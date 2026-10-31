/* brawler simulation: fighters, combos, grabs, weapons, enemy crowd AI with attack tokens,
 * bosses. Runs in fixed_update from the input snapshot and cw_rng only. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/anim.h"
#include "cartwheel/ik.h"
#include "cartwheel/input.h"
#include "cartwheel/nav.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/resource.h"
#include "cartwheel/vfs.h"
#include "game.h"

world_t W;
bool g_legacy_air; /* autopilot debug "legacyair 1": the v0.4.0 airborne stagger without gravity (the controls gate's control) */
int g_heroes[MAXP] = { D_ROOK, D_MIKA, D_JET, D_DOLLY }; /* who each seat plays (autopilot debug "hero <seat> <def>": close-ups and human-input runs) */
bool g_legacy_grab; /* autopilot debug "legacygrab 1": the grab as it was (the arms as the clip left them, 0.75 m apart, no hoist): the grab gate's control */
bool g_legacy_throw; /* autopilot debug "legacythrow 1": the old thrown-body test (it hit the fighters, never the enemies): the throw gate's control */
bool g_log_frozen; /* autopilot BRAWLER_LOG: name every enemy that stood frozen a second */
float g_cam[3] = { 1, 0, 0 };
bool g_freeze;
static cw_crowd crowd;
static cw_navmesh navm;

const char_def DEFS[D_COUNT] = {
  [D_ROOK] = { "Rook", "rook", 3.5f, 150, 1.25f, false, false, 0 },
  [D_MIKA] = { "Mika", "mika", 3.9f, 105, 1.0f, false, false, 0 },
  [D_JET] = { "Jet", "jet", 3.6f, 120, 1.1f, false, false, 0 },
  [D_THUG] = { "Thug", "thug", 2.0f, 42, 1.0f, false, false, 100 },
  [D_PUNK] = { "Punk", "punk", 2.4f, 36, 1.0f, false, false, 120 },
  [D_RUNNER] = { "Runner", "runner", 2.6f, 30, 1.0f, false, false, 150 },
  [D_KNIFER] = { "Knifer", "knifer", 2.3f, 48, 1.0f, false, false, 200 },
  [D_BRUISER] = { "Bruiser", "bruiser", 1.9f, 240, 1.2f, false, true, 600 },
  [D_THROWER] = { "Thrower", "thrower", 2.3f, 34, 1.0f, false, false, 180 },
  [D_GUARD] = { "Guard", "guard", 1.8f, 60, 1.0f, false, false, 250 },
  [D_FOREMAN] = { "The Foreman", "foreman", 1.9f, 360, 1.1f, true, false, 2000 },
  [D_MATRON] = { "The Matron", "matron", 2.5f, 380, 1.0f, true, false, 3000 },
  [D_RINGMASTER] = { "Ringmaster", "ringmaster", 2.6f, 300, 1.1f, false, true, 1500 }, /* a mid-stage mini boss in the arcade */
  [D_COLOSSUS] = { "Colossus", "colossus", 2.2f, 380, 1.3f, true, false, 8000 },
  [D_ARTHUR] = { "Arthur", "arthur", 2.7f, 380, 1.15f, true, false, 3000 },
  [D_WANKER] = { "Wanker", "wanker", 2.0f, 50, 1.1f, false, false, 160 }, /* a regular enemy: the haymaker and the shoulder barge */
  [D_CREEP] = { "Creep", "creep", 1.7f, 50, 0.9f, false, false, 180 }, /* a regular enemy who enjoys being hit: the coat flash, a lurch */
  [D_DOLLY] = { "Dolly", "dolly", 3.0f, 175, 1.45f, false, false, 0 }, /* the fourth fighter, the grappler: slowest and hardest to stop, reaches further to grab, holds on through jabs, throws hardest; a double-fist hammer, a headbutt, a running clothesline */
};

const atk_data ATK[] = {
  [A_PUNCH1] = { "punch1", 25, 8, 14, 8, 1.15f, 0.7f, 0, 0.6f },
  [A_PUNCH2] = { "punch2", 27, 9, 16, 9, 1.15f, 0.7f, 0, 0.6f },
  [A_FINISH] = { "finisher", 42, 20, 28, 14, 1.3f, 0.8f, 2, 0.0f },
  [A_KICK] = { "kick", 33, 14, 23, 12, 1.5f, 0.7f, 2, 0.0f },
  [A_JKICK] = { "jumpkick", 60, 1, 59, 10, 1.3f, 0.8f, 2, 0.0f },
  [A_SPIN] = { "spin", 42, 9, 33, 10, 1.7f, 1.7f, 2, 0.0f },
  [A_SWING] = { "swing", 37, 11, 26, 16, 1.7f, 0.8f, 2, 0.3f },
  [A_STAB] = { "stab", 30, 13, 20, 12, 1.3f, 0.6f, 1, 0.5f },
  [A_KNEE] = { "knee", 30, 9, 13, 6, 1.1f, 0.8f, 1, 0.0f },
  [A_SLAM] = { "slam", 66, 38, 46, 20, 3.4f, 3.4f, 2, 0.0f },
  [A_CHARGE] = { "kick", 33, 8, 26, 11, 1.3f, 0.8f, 2, 0.0f },
  [A_BSPIN] = { "spin", 42, 9, 33, 14, 2.1f, 2.1f, 2, 0.0f },
  [A_THROW] = { "throw", 40, 16, 16, 0, 0, 0, 0, 0.0f }, /* the bottle leaves the hand on tick 16 */
  [A_BASH] = { "punch2", 30, 10, 16, 9, 1.2f, 0.75f, 1, 0.6f }, /* the guard's shield shove */
  [A_RUSH] = { "run", 60, 4, 46, 16, 1.25f, 0.85f, 2, 0.0f }, /* a boss charges along its lane */
  [A_FLICK] = { "flick", 36, 22, 29, 12, 1.4f, 0.8f, 1, 0.35f }, /* Arthur snaps his jacket open, then a backhand */
  [A_JUKE] = { "thump", 26, 0, 0, 0, 0, 0, 0, 0.0f }, /* Arthur thumps the jukebox on tick 0: the blow is the shockwave (juke_tick) */
  [A_HAYMAKER] = { "haymaker", 42, 2, 9, 14, 1.4f, 0.8f, 2, 0.45f }, /* the wanker's looping right: the wind-up is the tell, the stumble after it the opening */
  [A_BARGE] = { "shoulder", 50, 6, 34, 11, 1.3f, 0.85f, 2, 0.0f }, /* the wanker lowers a shoulder and runs along the lane */
  [A_FLASH] = { "flash", 40, 2, 12, 8, 1.35f, 0.9f, 1, 0.5f }, /* the creep throws his coat open wide and lunges: a stagger (his own clip; the tell is its first 0.43 s) */
};
/* attacks that need the long (heavy) telegraph and a danger ring on the street */
static bool heavy_atk(int k) { return k == A_SLAM || k == A_SWING || k == A_BSPIN || k == A_RUSH || k == A_FLICK || k == A_HAYMAKER || k == A_BARGE || k == A_FLASH; }

/* ── character rigs: a pool per model, so a spawn never parses a glb twice ── */
typedef struct rig { int def; bool busy; cw_node root, model, anim, wnode, wmodel, blob, shield; const t3_gltf *gltf; spring_rig sr; int wkind; cw_node gore[3];
  /* the guard's shield (presentation): on his left forearm, or loose (dropped when he is floored or grabbed: it falls flat
     on the street and he takes it up again when he is back on his feet). blend: frames into the fall or the pick-up */
  cw_node ik[2]; /* a fighter's arms: hands on the collar of the body he holds (TwoBoneIK3D, only while he holds one) */
  bool sloose, spick; int sblend; t3_vec3 sp0, sp1; t3_quat sq0, sq1; } rig;
/* one glTF resource per model: every rig is an instance (cw_gltf_instantiate) sharing its geometry, textures and
   clips, so a crowd of one kind costs a skeleton each, not a parsed glb each */
static cw_res RES[D_COUNT], BLOB;
static bool clip_checked[D_COUNT];
static const float BODY_SCALE[D_COUNT] = { 1.08f, 0.98f, 1.0f, 1.0f, 0.96f, 0.94f, 1.0f, 1.28f, 1.0f, 1.08f, 1.4f, 1.22f, 1.15f, 1.7f, 1.15f, 1.06f, 1.0f, 1.15f };
static rig RIGS[96];
static int rig_n;
static cw_node world_node(void) { return cw_node_get(cw_node_get(cw_tree_root(cw_scene_app_tree()), "Level"), "World"); }
static int rig_of[MAXP + MAXE];

/* the pickups and weapons as models (tools/make-world.mjs: apple, bat, pipe, knife, coin, bottle), one glTF each,
   instanced; they used to be plain boxes (a gold box glowing in mid air read as a broken white square) */
static cw_res ITEM_RES[8];
static cw_res SHIELD_RES;
t3_object *item_model(int kind) {
  static const char *path[8] = { "models/apple.glb", "models/bat.glb", "models/pipe.glb", "models/knife.glb", "models/coin.glb", "models/bottle.glb", "models/comb.glb", "models/baton.glb" };
  if (kind < 0 || kind > 7) return NULL;
  if (!ITEM_RES[kind].id) ITEM_RES[kind] = cw_res_load(path[kind], CW_RES_GLTF);
  t3_object *o = cw_gltf_instantiate(ITEM_RES[kind]);
  if (!o) { cw_error("brawler: %s did not load", path[kind]); return NULL; }
  for (int i = 0; i < o->child_count; i++) o->children[i]->cast_shadow = true, o->children[i]->receive_shadow = true;
  o->cast_shadow = true, o->receive_shadow = true;
  return o;
}

bool g_hair_shadow; /* measurement: the hair casts as it used to */
static void set_shadows(t3_object *o) {
  if (o->type == T3_SKINNED_MESH || o->type == T3_MESH) o->cast_shadow = !(o->name && !strncmp(o->name, "Hair", 4)) || g_hair_shadow, o->receive_shadow = true; /* the alpha-tested hair cards are the dearest thing in the shadow map and read as nothing in a street shadow */
  for (int i = 0; i < o->child_count; i++) set_shadows(o->children[i]);
}

/* how a character takes a hit: the big men get scared, the little ones angry, the creep delighted (faces, reactions, emotes) */
int temper_of(int def) {
  static const int TEMPER[D_COUNT] = { [D_ROOK] = FACE_FEAR, [D_MIKA] = FACE_ANGER, [D_JET] = FACE_ANGER, [D_THUG] = FACE_ANGER, [D_PUNK] = FACE_ANGER, [D_RUNNER] = FACE_ANGER,
      [D_KNIFER] = FACE_ANGER, [D_BRUISER] = FACE_FEAR, [D_THROWER] = FACE_ANGER, [D_GUARD] = FACE_ANGER, [D_FOREMAN] = FACE_FEAR, [D_MATRON] = FACE_FEAR,
      [D_RINGMASTER] = FACE_FEAR, [D_COLOSSUS] = FACE_FEAR, [D_ARTHUR] = FACE_FEAR, [D_WANKER] = FACE_FEAR, [D_CREEP] = FACE_BLISS, [D_DOLLY] = FACE_ANGER };
  return def >= 0 && def < D_COUNT ? TEMPER[def] : FACE_PLAIN;
}
static rig *rig_get(int def) {
  for (int i = 0; i < rig_n; i++)
    if (!RIGS[i].busy && RIGS[i].def == def) { RIGS[i].busy = true; return &RIGS[i]; }
  if (rig_n >= (int)(sizeof RIGS / sizeof *RIGS)) return NULL;
  rig *r = &RIGS[rig_n++];
  memset(r, 0, sizeof *r);
  r->def = def;
  char path[64];
  snprintf(path, sizeof path, "models/%s.glb", DEFS[def].model);
  if (!RES[def].id) {
    RES[def] = cw_res_load(path, CW_RES_GLTF);
    const cw_gltf *g = cw_res_gltf(RES[def]);
    if (!g) { cw_error("brawler: %s did not load", path); rig_n--; return NULL; }
  }
  if (!BLOB.id) BLOB = cw_res_load("models/blob.glb", CW_RES_GLTF);
  r->gltf = cw_res_gltf(RES[def])->gltf;
  t3_object *inst = cw_gltf_instantiate(RES[def]);
  if (!inst) { cw_error("brawler: %s has no instance", path); rig_n--; return NULL; }
  char nm[32];
  snprintf(nm, sizeof nm, "Rig%d", rig_n);
  r->root = cw_node_new("Node3D", nm);
  r->model = cw_node_new("Node3D", "Model");
  r->anim = cw_node_new("AnimationPlayer", "Anim");
  r->wnode = cw_node_new("MeshInstance3D", "Weapon");
  r->wmodel = cw_node_new("Node3D", "WeaponModel");
  r->blob = cw_node_new("Node3D", "Blob");
  cw_node_add_child(world_node(), r->blob);
  { t3_object *bi = cw_gltf_instantiate(BLOB); if (bi) { cw_node3d_set_object(r->blob, bi); t3_release(bi); } }
  cw_node_add_child(world_node(), r->root);
  cw_node_add_child(r->root, r->model);
  cw_node_add_child(r->root, r->anim);
  cw_node_add_child(r->root, r->wnode);
  cw_node_add_child(r->root, r->wmodel);
  cw_node_set(r->wmodel, "visible", cw_val_bool(false));
  cw_node3d_set_object(r->model, inst);
  looks_bind((int)(r - RIGS), def, inst); /* its own copies of the materials a spawn recolours (looks.c) */
  set_shadows(inst);
  sb_bind(&r->sr, inst);
  { /* how the face takes a hit: the big men get scared, the little ones angry, the creep delighted */
    face_temper(&r->sr, temper_of(def));
  }
  t3_release(inst); /* the node holds it */
  for (int i = 0; i < r->gltf->animation_count; i++) {
    cw_anim a = cw_anim_from_t3_clip(r->gltf->animations[i], "Model");
    cw_anim_set_stored(a, false);
    /* the hit windows live in ATK (fixed ticks, so the simulation never waits on the render-side animation clock);
       a clip shorter than its window would swing at the air: say so once per model */
    for (int k = 1; k < A_COUNT && !clip_checked[def]; k++)
      if (k != A_JKICK && k != A_RUSH && k != A_BARGE && ATK[k].clip /* those two hold or loop their clip for the whole window */ && !strcmp(ATK[k].clip, r->gltf->animations[i]->name) && cw_anim_length(a) + 0.02f < (float)ATK[k].a1 / TPS)
        cw_warn("brawler: clip %s of %s is %.2f s, shorter than attack %d's hit window (%d ticks)", ATK[k].clip, DEFS[def].model, cw_anim_length(a), k, ATK[k].a1);
    cw_anim_player_add(r->anim, r->gltf->animations[i]->name, a);
    cw_anim_release(a);
  }
  clip_checked[def] = true;
  cw_node_set(r->wnode, "primitive", cw_val_enum("BOX"));
  cw_node_set(r->wnode, "size", cw_val_vec3(0.07f, 0.8f, 0.07f));
  cw_node_set(r->wnode, "visible", cw_val_bool(false));
  cw_node_set(r->wnode, "cast_shadow", cw_val_bool(true));
  if (def <= D_JET || def == D_DOLLY || def == D_ARTHUR) { /* the fighters grab, Arthur combs: an arm solver per side, off until wanted */
    for (int k = 0; k < 2; k++) {
      r->ik[k] = cw_node_new("TwoBoneIK3D", k ? "IkR" : "IkL");
      cw_node_add_child(r->model, r->ik[k]);
      cw_node_set(r->ik[k], "root_bone", cw_val_string(k ? "UpperArmR" : "UpperArmL"));
      cw_node_set(r->ik[k], "middle_bone", cw_val_string(k ? "LowerArmR" : "LowerArmL"));
      cw_node_set(r->ik[k], "end_bone", cw_val_string(k ? "HandR" : "HandL"));
      cw_node_set(r->ik[k], "use_pole", cw_val_bool(true));
      cw_node_set(r->ik[k], "active", cw_val_bool(false));
    }
  }
  if (def == D_GUARD) { /* the riot shield (models/shield.glb, make-world.mjs): world space, so it can lie where it fell */
    r->shield = cw_node_new("Node3D", "Shield");
    cw_node_add_child(world_node(), r->shield);
    if (!SHIELD_RES.id) SHIELD_RES = cw_res_load("models/shield.glb", CW_RES_GLTF);
    t3_object *o = cw_gltf_instantiate(SHIELD_RES);
    if (o) { set_shadows(o); cw_node3d_set_object(r->shield, o); t3_release(o); }
  }
  r->busy = true;
  return r;
}

/* idle variety: a neck roll now and then, light fighters bounce on their toes (clip names only, not simulation) */
static const char *idle_clip(const ent *e) {
  if ((W.tick + e->idx * 97 + e->def * 31) % 720 < 120) return "idle2";
  if (e->def == D_ARTHUR) return "idle_boxer"; /* fists up, a bounce on his toes */
  return (e->def == D_JET || e->def == D_MIKA || e->def == D_PUNK || e->def == D_RUNNER) ? "idle3" : "idle";
}
/* per character clips: Arthur's charge leads with his shoulder, his taunt runs a comb back through the pompadour */
static const char *atk_clip(const ent *e, int k) {
  if (e->def == D_DOLLY) { if (k == A_FINISH) return "hammer"; if (k == A_KICK) return "headbutt"; if (k == A_SPIN) return "lariat"; } /* her own moves, same windows */
  return e->def == D_ARTHUR && k == A_RUSH ? "shoulder" : ATK[k].clip;
}
static const char *taunt_clip(const ent *e) { return e->def == D_ARTHUR ? "comb" : "taunt"; }
/* the model faces +x or -x; frozen for a screenshot it turns three quarters to the camera */
float g_freeze_yaw = 0.45f; /* debug "freeze 1 <yaw>": how far a frozen body turns from the camera (1.57: side on) */
static float ent_yaw(const ent *e) { return g_freeze ? (float)e->face * g_freeze_yaw : (float)e->face * 1.5708f; }
static int ent_index(const ent *e) { return e->player ? (int)(e - W.p) : MAXP + (int)(e - W.e); }

/* a new scene (a game started again from the title) freed every rig's nodes: start the pool over */
int rig_count(void) { return rig_n; }
void char_preload(int def) {
  char path[64];
  snprintf(path, sizeof path, "models/%s.glb", DEFS[def].model);
  if (!RES[def].id) RES[def] = cw_res_load(path, CW_RES_GLTF);
  if (!BLOB.id) BLOB = cw_res_load("models/blob.glb", CW_RES_GLTF);
}
void chars_reset(void) { /* (the glTF resources stay loaded: the next game instances them again) */
  rag_reset();
  looks_reset();
  memset(RIGS, 0, sizeof RIGS);
  rig_n = 0;
  memset(&W, 0, sizeof W); /* every entity's node handles went with the scene */
}
void char_attach(ent *e) {
  rig *r = rig_get(e->def);
  if (!r) return;
  rig_of[ent_index(e)] = (int)(r - RIGS);
  e->root = r->root, e->model = r->model, e->anim = r->anim, e->wnode = r->wnode;
  cw_node_set(r->blob, "visible", cw_val_bool(true));
  cw_node_set(r->wmodel, "visible", cw_val_bool(false));
  e->clipname = NULL;
  cw_node_set(e->root, "visible", cw_val_bool(true));
  cw_node_set(e->anim, "speed_scale", cw_val_float(1));
  sb_reset(&r->sr);
  for (int k = 0; k < 3; k++) { if (r->gore[k].id && cw_node_valid(r->gore[k])) cw_node_free(r->gore[k]); r->gore[k] = (cw_node){ 0 }; } /* a fresh face for the next one */
  r->sloose = false, r->spick = false, r->sblend = 10; /* the shield back on his arm */
  char_play(e, "idle", 0);
}

/* gore on the characters (B-PT12): blood stamped on a bloodied face, on a fist that landed a big blow and on a weapon that
   drew blood, as mesh decals (cartwheel/decals.h MESH mode: clipped from the posed skinned mesh once and re-skinned to its
   skeleton, so it moves with the head or the hand). One of each per character life; they join a pool of 24 recycled decals
   (max_mesh_decals) and go with the rig when it is reused. Presentation only. */
enum { GORE_FACE, GORE_FIST, GORE_WEAPON };
static uint32_t gore_n;
static void gore_stamp(ent *e, int what) {
  if (!e->root.id || !e->model.id) return;
  rig *r = &RIGS[rig_of[ent_index(e)]];
  if (r->gore[what].id && cw_node_valid(r->gore[what])) return;
  if (what == GORE_WEAPON && !(e->weapon > 0 && e->weapon < 4)) return;
  t3_vec3 w = cw_skeleton_bone_world(e->model, what == GORE_FACE ? "Head" : "HandR");
  if (what == GORE_FACE) w.y += 0.1f * BODY_SCALE[e->def];
  if (what == GORE_WEAPON) w.y -= 0.25f;
  /* into the root's frame (the root turns the model by its yaw) */
  float yaw = ent_yaw(e), c = cw_cos(yaw), s = cw_sin(yaw), dx = w.x - e->x, dy = w.y - e->y, dz = w.z - e->z;
  t3_vec3 l = { c * dx - s * dz, dy, s * dx + c * dz };
  char nm[16];
  snprintf(nm, sizeof nm, "Gore%d", what);
  cw_node d = cw_node_new("Decal3D", nm);
  cw_node_add_child(r->root, d);
  uint32_t h = ++gore_n * 2654435761u;
  cw_node_set(d, "mode", cw_val_enum("MESH"));
  cw_node_set(d, "target", cw_val_string(what == GORE_WEAPON ? "../WeaponModel" : "../Model"));
  cw_node_set(d, "texture", cw_val_string("decals/blood.png"));
  cw_value reg = { CW_T_VEC4 };
  int cell = what == GORE_FACE ? ((h >> 8) & 1 ? 6 : 0) : ((h >> 8) & 1 ? 4 : 1);
  reg.v[0] = (float)(cell & 3) * 256, reg.v[1] = (float)(cell >> 2) * 256, reg.v[2] = 256, reg.v[3] = 256;
  cw_node_set(d, "region", reg);
  cw_node_set(d, "blend", cw_val_enum("MIX")); /* lit like the skin it is on */
  cw_node_set(d, "color", cw_val_color(1, 1, 1, 1));
  cw_node_set(d, "max_mesh_decals", cw_val_int(24));
  float sz = what == GORE_FACE ? 0.32f * BODY_SCALE[e->def] : 0.26f;
  cw_node_set(d, "size", cw_val_vec3(sz, sz, what == GORE_FACE ? 0.5f : 0.3f));
  if (what == GORE_FACE) { /* in front of the face, pointing back into it (the model faces +z in its root) */
    cw_node3d_set_position(d, (t3_vec3){ l.x, l.y, l.z + 0.2f });
    cw_node3d_set_rotation(d, (t3_vec3){ 0.25f, 0, (float)(h & 0xff) / 255.0f * 6.28f });
  } else { /* from above, down over the knuckles or the head of the weapon */
    cw_node3d_set_position(d, (t3_vec3){ l.x, l.y + 0.12f, l.z });
    cw_node3d_set_rotation(d, (t3_vec3){ -1.5708f, 0, (float)(h & 0xff) / 255.0f * 6.28f });
  }
  r->gore[what] = d;
}
/* secondary motion: every live character's springs, once per fixed tick after the animation */
float g_spring_motion; /* debug: summed speed of every spring tip, so a probe can see the springs move */
void chars_secondary(void) {
  g_spring_motion = 0;
  for (int i = 0; i < MAXP + MAXE; i++) {
    ent *e = i < MAXP ? &W.p[i] : &W.e[i - MAXP];
    if (!e->used || !e->root.id) continue;
    spring_rig *sr = &RIGS[rig_of[i]].sr;
    sb_step(sr, (t3_vec3){ e->x, e->y, e->z }, ent_yaw(e), 1.0f / TPS);
    { /* the face: the expression the clip asks for at this point of it (morph targets by name) */
      float u = 0;
      if (e->anim.id && e->clipname) {
        cw_anim a = cw_anim_player_get(e->anim, e->clipname);
        float L = a.id ? cw_anim_length(a) : 0;
        u = L > 0 ? (float)(cw_anim_player_position(e->anim) / L) : 0;
        if (u > 1) u = 1;
      }
      face_express(sr, e->clipname, u);
    }
    for (int k = 0; k < SB_MAX; k++) if (sr->b[k].bone) g_spring_motion += fabsf(sr->b[k].V.x) + fabsf(sr->b[k].V.y) + fabsf(sr->b[k].V.z);
  }
}
/* a hit shakes the hair, tails and belly away from the attacker */
void char_jolt(ent *e, float dir, float k) {
  if (!e->root.id) return;
  sb_jolt(&RIGS[rig_of[ent_index(e)]].sr, dir * 2.2f * k, 1.2f * k, 0);
}
void char_free(ent *e) {
  if (!e->root.id) return;
  if (rag_take_corpse(rig_of[ent_index(e)], e)) { e->root = (cw_node){ 0 }; return; } /* it lies a while and sinks (rag.c) */
  cw_node_set(e->wnode, "visible", cw_val_bool(false));
  cw_node_set(RIGS[rig_of[ent_index(e)]].wmodel, "visible", cw_val_bool(false));
  cw_node_set(e->root, "visible", cw_val_bool(false));
  cw_node_set(RIGS[rig_of[ent_index(e)]].blob, "visible", cw_val_bool(false));
  if (RIGS[rig_of[ent_index(e)]].shield.id) cw_node_set(RIGS[rig_of[ent_index(e)]].shield, "visible", cw_val_bool(false));
  cw_node3d_set_position(e->root, (t3_vec3){ 0, -50, 0 });
  RIGS[rig_of[ent_index(e)]].busy = false;
  e->root = (cw_node){ 0 };
}
ent *g_pose_e; const char *g_pose_clip; /* debug "pose <ent> <clip>": that body holds that clip (close-ups) */
void char_play(ent *e, const char *clip, float blend) {
  if (!e->anim.id) return;
  if (e == g_pose_e && g_pose_clip) clip = g_pose_clip;
  if (e->clipname == clip) return;
  e->clipname = clip;
  cw_anim_player_play(e->anim, clip, blend);
  cw_anim_player_seek(e->anim, 0, false);
}
static void char_restart(ent *e, const char *clip, float blend) {
  e->clipname = NULL;
  char_play(e, clip, blend);
}

/* the held weapon: its model goes on when it is picked up (the grip sits in the right hand, see char_sync) */
static void weapon_show(ent *e) {
  if (!e->root.id) return;
  rig *r = &RIGS[rig_of[ent_index(e)]];
  bool vis = e->weapon > 0 && e->weapon < 4;
  cw_node_set(r->wmodel, "visible", cw_val_bool(vis));
  if (r->gore[2].id && cw_node_valid(r->gore[2]) && (!vis || r->wkind != e->weapon)) { cw_node_free(r->gore[2]); r->gore[2] = (cw_node){ 0 }; } /* a clean weapon */
  if (vis && r->wkind != e->weapon) {
    t3_object *o = item_model(e->weapon);
    if (o) { cw_node3d_set_object(r->wmodel, o); t3_release(o); r->wkind = e->weapon; }
  }
}

/* ── rumble: the feel the camera shake used to give, on the pads (short, scaled, never left on: every call has a
   duration and the engine stops the motors after it) ── */
void rumble(int player, float low, float high, int ms) {
  for (int i = 0; i < W.players; i++) if (player < 0 || player == i) cw_input_rumble(i, low, high, ms);
}
static void rumble_ent(const ent *e, float low, float high, int ms) { if (e && e->player) rumble((int)(e - W.p), low, high, ms); }

/* ── small helpers ──────────────────────────────────────────────────── */
static float rf(float a, float b) { return cw_rng_range(&W.rng, a, b); }
static int ri(int a, int b) { return cw_rng_int(&W.rng, a, b); }
static float absf(float x) { return x < 0 ? -x : x; }
static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static bool alive(const ent *e) { return e->used && e->st != S_DEAD && e->hp > 0; }
static bool free_state(const ent *e) { return e->st == S_IDLE || e->st == S_WALK; }

float level_cam_halfw(void) { return 7.2f; }
/* what the game camera sees (position (cam_x, 3.9, 7.7), pitched down 0.3 rad, 40 degree vertical field, 16:9):
   the half width of the view at depth z and height y. The near lane sees much less than the far one. */
float view_halfw(float z, float y) {
  float depth = -((y - 3.9f) * 0.29552f + (z - 7.7f) * 0.95534f); /* along the view axis */
  return depth * 0.36397f * (16.0f / 9.0f);
}
/* a body (its middle, 1 m up) inside the picture with this much room to the edge */
bool on_screen(float x, float z, float margin) { float d = x - W.cam_x; return (d < 0 ? -d : d) < view_halfw(z, 1.0f) - margin; }

/* ── the crowd ──────────────────────────────────────────────────────── */
static void crowd_setup(void) {
  if (crowd.id) { cw_crowd_free(crowd); crowd = (cw_crowd){ 0 }; }
  if (navm.id) { cw_navmesh_free(navm); navm = (cw_navmesh){ 0 }; }
  extern float level_length(void);
  float len = level_length();
  cw_nav_geom g = { 0 };
  cw_nav_geom_add_box(&g, (t3_vec3){ len * 0.5f, -0.5f, 0 }, (t3_vec3){ len * 0.5f + 14, 0.5f, LANE_Z + 0.6f }, CW_NAV_AREA_WALK);
  cw_nav_config c = cw_nav_config_default();
  c.agent_radius = 0.3f;
  c.cell_size = 0.25f;
  navm = cw_navmesh_build(&c, &g);
  cw_nav_geom_free(&g);
  if (navm.id) crowd = cw_crowd_new(navm, MAXE + MAXP);
  else cw_error("brawler: navmesh build failed");
}
static uint64_t agent_add(float x, float z, float speed, bool is_player) {
  if (!crowd.id) return 0;
  cw_crowd_agent_params p = cw_crowd_agent_params_default();
  p.radius = 0.46f;
  p.max_speed = speed;
  p.max_accel = 14;
  p.neighbor_dist = 3.0f;
  p.max_neighbors = 8;
  p.time_horizon = 1.0f;
  p.arrive_dist = 0.1f;
  p.slowdown_dist = 0.3f;
  p.avoidance_priority = is_player ? 0.0f : 0.5f;
  return cw_crowd_add(crowd, (t3_vec3){ x, 0, z }, &p).id;
}
static cw_crowd_agent AG(uint64_t id) { cw_crowd_agent a = { .id = id }; return a; }

/* ── spawning ───────────────────────────────────────────────────────── */
static void ent_init(ent *e, int def, bool player, float x, float z) {
  memset(e, 0, sizeof *e);
  e->used = true, e->player = player, e->def = def, e->x = x, e->z = z;
  e->face = player ? 1 : -1;
  /* more fighters: tougher enemies (35 percent of a fighter each); a boss 22 percent (each fighter also brings more of
     the crowd and the helpers to deal with, so the fight still ends a little sooner with four than alone) */
  float per = DEFS[def].boss ? 0.22f : 0.35f;
  e->hp = e->maxhp = DEFS[def].hp * (player ? 1.0f : 1.0f + per * (float)(W.players > 1 ? W.players - 1 : 0));
  e->st = S_IDLE, e->target = -1, e->victim = -1, e->holder = -1;
  e->cd = 40 + ri(0, 60);
  e->slot = ri(0, 9);
  e->mood_t = ri(30, 120), e->side_pref = 0, e->orbit = rf(0, 6.28f), e->lx = x, e->lz = z;
  e->agent = agent_add(x, z, DEFS[def].speed, player);
  char_attach(e);
}

int ent_spawn_enemy(int def, float x, float z) {
  for (int i = 0; i < MAXE; i++)
    if (!W.e[i].used) {
      ent_init(&W.e[i], def, false, x, z);
      W.e[i].idx = i;
      if (def == D_WANKER) W.wanker.spawned++;
      if (def == D_CREEP) W.creep.spawned++;
      W.e[i].look = look_pick(def, i); /* its own skin tone and clothes, never another's on the street (looks.c) */
      if (W.e[i].root.id) looks_apply(rig_of[MAXP + i], W.e[i].look);
      if (DEFS[def].boss) { W.boss_idx = i; W.boss_active = true; }
      return i;
    }
  return -1;
}
static void ent_remove(ent *e) {
  if (e->agent && crowd.id) cw_crowd_remove(crowd, AG(e->agent));
  if (e->token) W.tokens_out--;
  char_free(e);
  e->used = false;
}

int token_leak(void) { int held = 0; for (int i = 0; i < MAXE; i++) if (W.e[i].used && W.e[i].token) held++; return W.tokens_out - held; }
static int enemies_alive(void) {
  int n = 0;
  for (int i = 0; i < MAXE; i++) if (W.e[i].used && W.e[i].st != S_DEAD) n++;
  return n;
}

/* ── damage ─────────────────────────────────────────────────────────── */
static void drop_weapon(ent *e) {
  if (!e->weapon) return;
  for (int i = 0; i < MAXITEM; i++)
    if (!W.item[i].used) {
      W.item[i] = (item_t){ .used = true, .kind = e->weapon, .x = e->x + (float)e->face * 0.5f, .z = e->z, .y = 0.6f };
      break;
    }
  e->weapon = 0;
  weapon_show(e);
}

static void release_pair(ent *victim) {
  if (victim->holder >= 0) {
    ent *h = victim->holder < MAXP ? &W.p[victim->holder] : &W.e[victim->holder - MAXP];
    if (h->victim == ent_index(victim)) { h->victim = -1; if (h->st == S_GRAB) h->st = S_IDLE; }
    victim->holder = -1;
  }
}

/* the unfair-hit audit: where the blow came from (an attacker, or a bottle in flight) */
static const float *g_src;
#ifdef BRAWLER_AUTOPILOT
bool g_no_screen_rule; /* control for the audit: enemies may attack from off camera (BRAWLER_NO_SCREEN_RULE) */
#else
static const bool g_no_screen_rule = false;
#endif
/* the guard's shield: stops a light blow from the front while it stands or walks; kicks, jump kicks, the finisher,
   weapons, throws and the special go through it, and so does anything from behind. Three blocks break the guard. */
static bool guard_blocks(ent *v, const ent *a, int knock) {
  if (v->player || v->def != D_GUARD || !a || knock >= 2 || a->st == S_THROWN) return false;
  if (!(v->st == S_IDLE || v->st == S_WALK || (v->st == S_ATK && v->t < 0))) return false;
  if ((a->x - v->x) * (float)v->face <= 0) return false; /* from behind */
  W.st.blocks++;
  fx_hit(v->x + (float)v->face * 0.45f, v->y + 1.1f, v->z, 0, v->face);
  sfx(SFX_BREAK);
  W.hitstop = 3;
  if (a->player) { ((ent *)a)->x -= (float)a->face * 0.25f; }
  if (++v->guard_hits >= 3) { /* guard broken: the shield drops and it reels */
    v->guard_hits = 0;
    v->st = S_HIT, v->t = 0, v->stun = 50;
    char_restart(v, "hit_hard", 0.03f);
    if (v->token) { v->token = 0; W.tokens_out--; }
  } else char_restart(v, "block", 0.03f);
  return true;
}

void ent_hurt(ent *v, ent *a, float dmg, int knock, float fx) {
  (void)fx;
  if (!alive(v) || v->invuln > 0) return;
  if (guard_blocks(v, a, knock)) return;
  bool boss = DEFS[v->def].boss || DEFS[v->def].mini;
  /* the wanker's haymaker wind-up shrugs off a light blow (only a heavy one stops it): with every jab interrupting it,
     not one in the self-play runs ever landed. The tell is long, so the answer is to step out of the lane */
  bool armour = boss || (v->def == D_WANKER && v->st == S_ATK && v->atk == A_HAYMAKER);
  /* the creep leans into a light blow (no flinch) and it does him a little good: 2 of its damage back, never past his
     health; a kick, a throw or anything heavy still floors him */
  if (v->def == D_CREEP && knock == 0 && dmg > 0 && v->hp > dmg) { armour = true; W.creep.shrugged++; float h0 = v->hp; v->hp += 2; if (v->hp > v->maxhp) v->hp = v->maxhp; W.creep.healed += (int)(v->hp - h0 + 0.5f); }
  if (v->player && dmg > 0 && a != v) { /* audit: a fighter hurt by something the camera does not show, or by an enemy still walking in */
    float sx = g_src ? g_src[0] : a ? a->x : v->x, sz = g_src ? g_src[1] : a ? a->z : v->z;
    if (!on_screen(sx, sz, 0) || (a && !a->player && !g_src && a->ptimer < 20)) W.st.unfair++;
    W.st.dmg_taken += (int)dmg;
  }
  if (!v->player && a && a->player && dmg > 0) {
    if (W.st.first_hit < 0) W.st.first_hit = W.tick - W.st.t0;
    W.st.hits++;
    if (DEFS[v->def].boss) W.st.boss_hits++;
    if (a->atk > 0 && a->atk < A_COUNT) W.st.atk_hits[a->atk]++;
  }
  if (v->player && v->st == S_JUMP && knock < 2) knock = 1;
  if (v->def == D_WANKER && knock == 1) knock = 2; /* out of shape (B-PT18): a stagger blow puts him on the floor */
  v->hp -= dmg;
  if (DEFS[v->def].boss && v->phase < 2) { /* no phase is skipped: it cannot fall below the next one until that one began */
    float floor_hp = v->phase == 0 ? v->maxhp * 0.335f : 1.0f;
    if (v->hp < floor_hp) v->hp = floor_hp;
  }
  v->hurtflash = 1;
  int dir = a ? (a->x <= v->x ? 1 : -1) : -v->face;
  W.hitstop = knock >= 2 ? 8 : 4; /* frozen frames: 4 on a light blow, 8 on a heavy one */
  W.shake = knock >= 2 ? 10 : 4;
  W.shake_amp = knock >= 2 ? 0.12f : 0.05f;
  if (dmg > 0 && knock >= 2) { rumble_ent(v, 0.35f, 0.9f, 140); rumble_ent(a, 0.25f, 0.75f, 110); } /* a big hit: short and sharp, to both ends of it */
  else if (dmg > 0 && knock >= 1) { rumble_ent(v, 0.2f, 0.6f, 90); rumble_ent(a, 0.12f, 0.45f, 70); }
  else if (dmg > 0 && v->player) rumble_ent(v, 0.0f, 0.35f, 60);
  fx_hit(v->x, v->y + 1.1f, v->z, knock, dir);
  if (!v->player && dmg > 0 && temper_of(v->def) != FACE_PLAIN) v->react = 48; /* the reaction and the emote */
  if (dmg > 0) fx_blood(v->x + (float)dir * 0.12f, v->y + 1.45f, v->z, dir, knock); /* blood from the face and the body */
  if (dmg > 0 && knock >= 1 && v->hp - dmg < v->maxhp * 0.55f) gore_stamp(v, GORE_FACE); /* a bloodied face after the big ones */
  if (dmg > 0 && a && a->player && knock >= 2 && !a->weapon) gore_stamp(a, GORE_FIST);
  if (dmg > 0 && a && a->player && a->weapon && (a->atk == A_SWING || a->atk == A_STAB)) gore_stamp(a, GORE_WEAPON);
  char_jolt(v, (float)dir, knock >= 2 ? 1.6f : 1.0f);
  sfx(knock >= 2 ? SFX_HIT3 : ((int)v->hp & 1) ? SFX_HIT1 : SFX_HIT2);
  if (v->def == D_DOLLY && v->st == S_GRAB && knock == 0 && v->hp > 0) return; /* she holds on through a jab (only a stagger or worse makes her let go) */
  if (a && a->player) {
    a->score += (int)dmg * 10;
    W.combo_n++, W.combo_t = 100;
    W.total_score += (int)dmg * 10;
  }
  release_pair(v);
  if (v->victim >= 0) { /* a grabber that is hit lets go */
    ent *u = v->victim < MAXP ? &W.p[v->victim] : &W.e[v->victim - MAXP];
    u->holder = -1;
    if (u->st == S_GRABBED) { u->st = u->y > 0 ? S_FALL : S_IDLE; u->vx = u->vy = 0; u->pitch = 0; } /* dropped from the hoist: it falls */
    v->victim = -1;
  }
  if (v->token) { v->token = 0; W.tokens_out--; }
  v->ranged = 0;
  if (v->player && v->weapon && knock >= 2) drop_weapon(v);
  v->face = dir > 0 ? -1 : 1; /* turn toward the attacker */
  bool die = v->hp <= 0;
  if (die || knock >= 2 || (!boss && knock >= 1 && v->st == S_HIT)) {
    v->st = S_FALL, v->t = 0;
    v->vx = (float)dir * (die ? 4.5f : 3.4f);
    v->vy = die ? 5.5f : 4.5f;
    v->hitmask = 0;
    v->invuln = 20;
    if (boss && !die) v->cd = 70;
    char_restart(v, "knockdown", 0.03f);
    if (die) {
      if (!v->player) {
        W.kills++;
        if (v->def == D_WANKER) W.wanker.kills++;
        if (v->def == D_CREEP) W.creep.kills++;
        if (a && a->player) a->score += DEFS[v->def].score;
        W.total_score += DEFS[v->def].score;
        drop_weapon(v);
        if (DEFS[v->def].boss) { W.shake = 40; W.shake_amp = 0.2f; rumble(-1, 0.8f, 0.6f, 600); W.slow = 90; W.st.boss_t1 = W.tick, W.st.boss_f1 = W.frames; }
        /* a coin or two */
        for (int k = 0; k < (DEFS[v->def].boss ? 6 : 1); k++)
          for (int i = 0; i < MAXITEM; i++)
            if (!W.item[i].used) { W.item[i] = (item_t){ .used = true, .kind = 4, .x = v->x + rf(-0.6f, 0.6f), .z = v->z + rf(-0.4f, 0.4f), .y = 0.5f }; break; }
      }
      sfx(SFX_KO);
    }
  } else if (!armour || v->st != S_ATK) {
    /* bosses and the mini boss do not flinch out of an attack (nor the wanker out of his haymaker) */
    if (!(armour && v->st == S_ATK)) {
      v->st = S_HIT, v->t = 0;
      v->stun = knock >= 1 ? 22 : 15;
      v->x += (float)dir * 0.18f;
      bool body = a && (a->atk == A_KICK || a->atk == A_KNEE || a->atk == A_CHARGE || a->atk == A_SWING);
      int tp = v->player ? FACE_PLAIN : temper_of(v->def);
      char_restart(v, tp == FACE_FEAR ? "react_fear" : tp == FACE_ANGER ? "react_rage" : tp == FACE_BLISS ? "react_bliss" : knock >= 1 ? "hit_hard" : body ? "hit_gut" : "hit", 0.02f);
    }
  }
  if (boss && v->st != S_FALL && v->st != S_HIT) v->hurtflash = 1;
}

/* ── attacks ────────────────────────────────────────────────────────── */
static void start_attack(ent *e, akind k) {
  e->st = S_ATK, e->atk = k, e->t = 0, e->hitmask = 0, e->chain_ok = 0;
  const atk_data *a = &ATK[k];
  if (!e->player) { /* a readable telegraph: guard pose and a flash before the swing (12+ frames, 24+ for heavies) */
    e->t = k == A_JUKE ? -36 : k == A_RUSH || k == A_BARGE ? -40 : heavy_atk(k) ? -26 : k == A_THROW ? -18 : -14;
    /* the jukebox thump: the raised fist is the tell; the haymaker: its own wind-up (the first 0.43 s of the clip, 26 ticks) */
    char_restart(e, k == A_JUKE ? "thump" : k == A_HAYMAKER ? "haymaker" : k == A_FLASH ? "flash" : "block", k == A_JUKE ? 0.1f : 0.06f);
    e->hurtflash = 1.4f;
  } else char_restart(e, atk_clip(e, k), 0.04f);
  if (k == A_SLAM || k == A_SPIN || k == A_BSPIN) sfx(SFX_WHIFF);
  if (k == A_HAYMAKER) W.wanker.hay++;
  if (k == A_BARGE) W.wanker.barge++;
  if (k == A_FLASH) W.creep.flash++;
}

static float body_size(const ent *e);
static bool in_box(const ent *a, const ent *v, float reach, float width) {
  /* reach is centre to centre: a big body (a boss, the mini boss) is reached and reaches further by its girth. Bodies
     are kept 1.2 m apart along the street from a boss (separate_bodies), and a 1.15 m punch never touched one face on:
     only from a diagonal, where the bots and the boss both stood waiting for the other to line up (the boss time to kill
     runs: up to 12 minute stalls, and slow fights) */
  reach += 0.6f * (body_size(a) + body_size(v) - 2.0f);
  float dx = (v->x - a->x) * (float)a->face;
  return dx > -0.25f && dx < reach && absf(v->z - a->z) < (width > 1.2f ? width : 0.62f) && absf(v->y - a->y) < 1.3f;
}
static bool in_round(const ent *a, const ent *v, float r) {
  float dx = v->x - a->x, dz = v->z - a->z;
  return dx * dx + dz * dz < r * r && absf(v->y - a->y) < 1.4f;
}

#ifdef BRAWLER_AUTOPILOT
float g_bot_dmg = 3.0f;
uint64_t g_seed_salt; /* BRAWLER_SEED: another match from the same rules (measurement runs; 0 in every gate) */
#else
static const uint64_t g_seed_salt = 0;
#endif
static void do_hits(ent *a) {
  const atk_data *ad = &ATK[a->atk];
  if (a->t < ad->a0 || a->t > ad->a1) return;
  float mul = DEFS[a->def].dmg * (a->player ? 1.0f : 0.8f);
  float dmg = ad->dmg * mul;
#ifdef BRAWLER_AUTOPILOT
  if (a->player) dmg *= g_bot_dmg; /* the bots are poor fighters: the soak needs to reach the bosses (BRAWLER_BOTDMG) */
#endif
  if (a->weapon && (a->atk == A_SWING || a->atk == A_STAB)) dmg *= a->weapon == 2 ? 1.25f : 1.0f;
  bool round = a->atk == A_SPIN || a->atk == A_SLAM || a->atk == A_BSPIN;
  bool hit_any = false;
  if (!a->player && !g_no_screen_rule && !on_screen(a->x, a->z, 0)) return; /* never a blow from off camera */
  /* targets: the other team */
  for (int i = 0; i < (a->player ? MAXE : MAXP); i++) {
    ent *v = a->player ? &W.e[i] : &W.p[i];
    if (!alive(v) || (a->hitmask & (1u << (i & 31)))) continue;
    if (v->st == S_DOWN || v->st == S_FALL || v->st == S_DEAD || v->st == S_GRABBED || v->st == S_THROWN) continue;
    bool ok = round ? in_round(a, v, ad->reach) : in_box(a, v, ad->reach, ad->width);
    if (a->atk == A_SLAM && a->y > 0.2f) ok = false;
    if (!ok) continue;
    a->hitmask |= 1u << (i & 31);
    int knock = (int)ad->knock;
    if (a->atk == A_FINISH && a->combo != 2) knock = 1;
    if (a->atk == A_HAYMAKER) W.wanker.hay_hits++;
    if (a->atk == A_BARGE) W.wanker.barge_hits++;
    if (a->atk == A_FLASH) W.creep.flash_hits++;
    ent_hurt(v, a, dmg, knock, 0);
    hit_any = true;
  }
  if (a->player) { /* props */
    for (int i = 0; i < MAXPROP; i++) {
      prop_t *p = &W.prop[i];
      if (!p->used || (a->hitmask & (1u << (16 + (i & 15))))) continue;
      float dx = (p->x - a->x) * (float)a->face;
      if ((round ? ((p->x - a->x) * (p->x - a->x) + (p->z - a->z) * (p->z - a->z) < ad->reach * ad->reach) : (dx > -0.3f && dx < ad->reach + 0.3f && absf(p->z - a->z) < 0.7f))) {
        a->hitmask |= 1u << (16 + (i & 15));
        prop_hit(p);
        hit_any = true;
      }
    }
  }
  if (hit_any && a->weapon && (a->atk == A_SWING || a->atk == A_STAB) && --a->wdur <= 0) {
    fx_debris(a->x + (float)a->face * 0.8f, 1.0f, a->z, 0xb09060);
    sfx(SFX_BREAK);
    a->weapon = 0;
    weapon_show(a);
  }
  if (a->atk == A_SLAM && a->t == ad->a0) { fx_dust(a->x + (float)a->face * 1.6f, a->z, 3.0f); W.shake = 14; W.shake_amp = 0.14f; rumble(-1, 0.75f, 0.2f, 260); sfx(SFX_SLAM); }
}

/* ── thrown bottles ─────────────────────────────────────────────────── */
static void proj_throw(ent *e) {
  for (int i = 0; i < 8; i++)
    if (!W.proj[i].used) {
      W.proj[i] = (struct proj){ .used = true, .x = e->x + (float)e->face * 0.5f, .y = 1.45f, .z = e->z, .vx = (float)e->face * 8.5f, .owner = (int)(e - W.e) };
      sfx(SFX_WHIFF);
      return;
    }
}
static void proj_update(void) {
  for (int i = 0; i < 8; i++) {
    struct proj *p = &W.proj[i];
    if (!p->used) continue;
    p->t++;
    p->x += p->vx / TPS;
    p->y = 1.45f - 0.35f * (float)p->t / 60.0f; /* a flat arc: it drops a little over a second */
    bool gone = p->t > 90 || absf(p->x - W.cam_x) > view_halfw(p->z, p->y) + 1.0f;
    for (int k = 0; k < MAXP && !gone; k++) {
      ent *v = &W.p[k];
      if (!alive(v) || v->st == S_DOWN || v->st == S_FALL || v->st == S_THROWN || v->st == S_GRABBED) continue;
      /* a bottle hits at chest height: a jump clears it, a step to another lane dodges it */
      if (absf(v->x - p->x) < 0.4f && absf(v->z - p->z) < 0.38f && v->y < 0.7f) {
        float src[2] = { p->x, p->z };
        ent *o = &W.e[p->owner];
        g_src = src;
        ent_hurt(v, o->used ? o : NULL, 9, 0, 0);
        g_src = NULL;
        fx_debris(p->x, p->y, p->z, 0x50a060);
        sfx(SFX_BREAK);
        gone = true;
      }
    }
    if (gone) { p->used = false; fx_debris(p->x, p->y, p->z, 0x406a48); }
  }
}

/* ── shared states ──────────────────────────────────────────────────── */
static void arena_clamp(ent *e) {
  /* fighters stay inside the picture: the view is narrower in the near lane */
  e->z = clampf(e->z, -LANE_Z, LANE_Z);
  /* enemies walk in from off camera; once they have been in the picture they stay in it (one standing just outside,
     behind fighters held at the edge, could neither be reached nor reach anyone: a soft lock the soak found) */
  /* the clamp sits inside the 0.3 m margin an enemy needs to be allowed to attack ('ready'): held exactly on that edge
     by fighters pressing on it, a boss was never ready, never attacked and was never reached (the boss time to kill
     runs found 12 minute stalls with two fighters against the Foreman at the left edge) */
  float hw = e->player ? view_halfw(e->z, 1.0f) - 0.6f : e->ptimer >= 20 ? view_halfw(e->z, 1.0f) - 0.45f : level_cam_halfw() + 3.0f;
  e->x = clampf(e->x, W.cam_x - hw, W.cam_x + hw);
}

/* how far a held body stands from its holder: their chests just apart, whatever their sizes */
static float grab_dist(const ent *h, const ent *v) { return g_legacy_grab ? 0.75f : 0.34f * (BODY_SCALE[h->def] + BODY_SCALE[v->def]); }
/* returns true when the shared state handled this tick */
static bool common_state(ent *e) {
  switch (e->st) {
  case S_HIT:
    /* a body hit in the air keeps falling while it reels (B-PT17: a fighter hit by a light blow in mid jump hung in the air
       for the whole stagger, then walked on at that height: 254 frames off the ground in a test, a normal jump is 46) */
    if (e->y > 0 && !g_legacy_air) {
      e->y += e->vy / TPS, e->vy -= 18.0f / TPS;
      if (e->y <= 0) e->y = 0, e->vy = 0;
    }
    if (++e->t >= e->stun) { e->st = S_IDLE; e->t = 0; }
    return true;
  case S_FALL:
    e->t++;
    e->x += e->vx / TPS, e->y += e->vy / TPS;
    e->vy -= 18.0f / TPS * 1.0f;
    e->vx *= 0.985f;
    if (e->y <= 0 && e->vy < 0) {
      e->y = 0;
      e->st = S_DOWN, e->t = 0;
      fx_dust(e->x, e->z, 1.2f);
      if (e->hp <= 0) fx_stain(e->x - (float)e->face * 0.55f, e->z, 2.0f, 2); /* a pool spreading under the head (it fell away from the blow) */
      else fx_stain(e->x - (float)e->face * 0.5f, e->z, e->hp < e->maxhp * 0.6f ? 0.6f : 0.42f, 1); /* a body that hits the street leaves a splat, bigger when it is hurt */
      W.shake = 6, W.shake_amp = 0.06f;
      rumble_ent(e, 0.25f, 0.1f, 90); /* a knockdown: light */
      char_play(e, e->hp <= 0 ? "dead" : "down", 0.02f);
      if (e->hp <= 0) { e->st = S_DEAD; if (e->player) W.st.deaths++, W.run_deaths++; }
    }
    return true;
  case S_THROWN: {
    e->t++;
    e->x += e->vx / TPS, e->y += e->vy / TPS;
    e->vy -= 18.0f / TPS;
    /* a thrown body bowls over the enemies it flies into (its own side: the fighters threw it). It used to test the other
       side, so a thrown thug knocked down the fighters it passed and flew through every enemy (the 0.1.1 review) */
    if (!g_legacy_throw) {
      ent *thr = e->thrower >= 0 && e->thrower < MAXP ? &W.p[e->thrower] : NULL;
      for (int i = 0; i < MAXE; i++) {
        ent *v = &W.e[i];
        if (!alive(v) || v == e || v->st == S_DOWN || v->st == S_FALL || v->st == S_THROWN || v->st == S_GRABBED || (e->bowled >> i & 1)) continue;
        float bx = v->x - e->x, bz = v->z - e->z, br = 0.55f + 0.4f * body_size(v);
        if (bx * bx + bz * bz >= br * br || e->y > 1.7f) continue;
        e->bowled |= 1ull << i;
        float hp0 = v->hp;
        bool armour_boss = DEFS[v->def].boss;
        ent_hurt(v, e, 10, armour_boss ? 1 : 2, 0); /* a boss takes the blow but keeps his feet */
        if (v->hp < hp0) {
          W.st.bowled++;
          e->vx *= 0.8f; /* each body it hits takes a little of its speed */
          if (thr) {
            int pts = (int)(hp0 - v->hp) * 10 + (v->hp <= 0 ? DEFS[v->def].score : 0);
            thr->score += pts, W.total_score += pts, W.combo_n++, W.combo_t = 100;
            W.st.hits++;
          }
        }
      }
      for (int i = 0; i < MAXPROP; i++) { /* and smashes the crates and barrels in its way */
        prop_t *p = &W.prop[i];
        if (!p->used || (e->bowled >> (MAXE + (i & 15)) & 1)) continue;
        float dx = p->x - e->x, dz = p->z - e->z;
        if (dx * dx + dz * dz < 0.8f * 0.8f && e->y < 1.2f) { e->bowled |= 1ull << (MAXE + (i & 15)); prop_hit(p); }
      }
    } else
      for (int i = 0; i < (e->player ? MAXE : MAXP); i++) { /* legacy: the throw gate's control */
        ent *v = e->player ? &W.e[i] : &W.p[i];
        if (!alive(v) || v == e || v->st == S_DOWN || v->st == S_FALL || v->st == S_THROWN || (e->hitmask & (1u << (i & 31)))) continue;
        if (in_round(e, v, 0.9f)) { e->hitmask |= 1u << (i & 31); ent_hurt(v, e, 10, 2, 0); }
      }
    if (e->y <= 0 && e->vy < 0) {
      e->y = 0;
      fx_dust(e->x, e->z, 1.6f);
      rumble(-1, 0.55f, 0.15f, 180); /* a thrown body slams into the street: heavy */
      e->vx *= 0.3f;
      e->st = S_FALL, e->t = 0, e->vy = 2.0f;
      char_restart(e, "knockdown", 0.02f); /* (a zero blow here used to stand a surviving body up in a flinch) */
    }
    return true;
  }
  case S_DOWN:
    e->t++;
    if (e->t > 48) {
      e->st = S_GETUP, e->t = 0;
      char_restart(e, "getup", 0.02f);
    }
    return true;
  case S_GETUP:
    if (++e->t > 54) { e->st = S_IDLE, e->t = 0; e->invuln = 40; char_play(e, idle_clip(e), 0.1f); }
    return true;
  case S_DEAD:
    e->t++;
    return true;
  case S_GRABBED: {
    ent *h = e->holder < 0 ? NULL : (e->holder < MAXP ? &W.p[e->holder] : &W.e[e->holder - MAXP]);
    if (!h || !alive(h) || h->victim != ent_index(e)) { e->st = e->y > 0 ? S_FALL : S_IDLE; e->vx = e->vy = 0; e->pitch = 0; e->holder = -1; return true; }
    float d = grab_dist(h, e); /* chest to chest: scaled to the two bodies */
    float k = 0;
    if (h->st == S_THROW && !g_legacy_grab) { /* the hoist: up over his head, tipped flat, before it goes */
      k = cw_clampf((float)h->t / 15.0f, 0, 1);
      k = k * k * (3 - 2 * k);
    }
    e->x = h->x + (float)h->face * (d + (0.95f - d) * k), e->z = h->z, e->y = 1.85f * k;
    e->pitch = 1.4f * k;
    e->face = -h->face;
    return true;
  }
  default: return false;
  }
}

static void apply_invuln(ent *e) {
  if (e->invuln > 0) e->invuln--;
  if (e->hurtflash > 0) e->hurtflash -= 0.1f;
}

/* ── players ────────────────────────────────────────────────────────── */
static void player_start_attack_ground(ent *e, const inputs *in) {
  if (in->special && e->hp > e->maxhp * 0.1f && e->st != S_GRAB) {
    e->hp -= e->maxhp * 0.05f;
    start_attack(e, A_SPIN);
    e->invuln = 20;
    return;
  }
  if (in->atk) {
    if (e->weapon) start_attack(e, e->weapon == 3 ? A_STAB : A_SWING);
    else {
      akind k = A_PUNCH1;
      if (e->combo_t > 0) k = e->combo == 1 ? A_PUNCH2 : e->combo == 2 ? A_FINISH : A_PUNCH1;
      start_attack(e, k);
      e->combo = k == A_PUNCH1 ? 1 : k == A_PUNCH2 ? 2 : 3;
      e->combo_t = 0;
      if (k == A_FINISH) e->combo = 0;
    }
    return;
  }
  if (in->kick) start_attack(e, A_KICK);
}

static void player_update(int pi, const inputs *in) {
  ent *e = &W.p[pi];
  if (!e->used) return;
  apply_invuln(e);
  if (e->hold > 0) { e->hold--; return; }
  if (e->combo_t > 0) e->combo_t--;
  if (common_state(e)) {
    if (e->st == S_DEAD) {
      if (e->t > 100) {
        if (e->lives > 0) { /* back in with a flash of invulnerability */
          e->lives--;
          e->hp = e->maxhp;
          e->x = W.cam_x - 3.0f, e->y = 4.0f, e->vy = 0;
          e->st = S_JUMP, e->t = 0, e->invuln = 150;
          char_play(e, "jump", 0.05f);
          drop_weapon(e);
        }
      }
    } else if (e->st == S_FALL && e->hp <= 0 && !(e->lives > 0)) {
      /* last life: stays down */
    }
    arena_clamp(e);
    return;
  }
  if ((e->st == S_IDLE || e->st == S_WALK) && e->y > 0 && !g_legacy_air) { /* off the ground and not jumping (whatever left it there): fall */
    e->st = S_JUMP, e->t = 0, e->atk = A_NONE, e->vx = 0;
    if (e->vy > 0) e->vy = 0;
  }
  switch (e->st) {
  case S_IDLE:
  case S_WALK: {
    float spd = DEFS[e->def].speed;
    float mx = in->mx, mz = in->mz;
    e->x += mx * spd / TPS, e->z += mz * spd * 0.8f / TPS;
    if (mx > 0.2f) e->face = 1;
    if (mx < -0.2f) e->face = -1;
    bool mv = mx * mx + mz * mz > 0.04f;
    e->st = mv ? S_WALK : S_IDLE;
    char_play(e, mv ? (spd > 3.5f ? "run" : "walk") : idle_clip(e), 0.12f);
    if (in->jump) { e->st = S_JUMP, e->vy = 6.8f, e->vx = mx * spd, e->t = 0; char_restart(e, "jump", 0.05f); break; }
    if (in->grab) { /* grab a body in front, or a weapon underfoot */
      int best = -1;
      for (int i = 0; i < MAXE; i++) {
        ent *v = &W.e[i];
        if (!alive(v) || DEFS[v->def].boss || v->st == S_DOWN || v->st == S_FALL || v->st == S_THROWN || v->st == S_GRABBED || v->st == S_DEAD) continue;
        bool reach = e->def == D_DOLLY ? in_box(e, v, 1.4f, 1.25f) && absf(v->z - e->z) < 0.85f : in_box(e, v, 1.05f, 0.7f);
        if (reach && (best < 0 || absf(v->z - e->z) < absf(W.e[best].z - e->z))) best = i;
      }
      if (best >= 0) {
        ent *v = &W.e[best];
        e->st = S_GRAB, e->t = 0, e->victim = MAXP + best, e->knees = 0;
        v->st = S_GRABBED, v->holder = pi, v->t = 0;
        if (v->token) { v->token = 0; W.tokens_out--; }
        char_restart(e, "grab", 0.05f);
        char_restart(v, "grabbed", 0.05f);
        break;
      }
    }
    player_start_attack_ground(e, in);
    break;
  }
  case S_JUMP: {
    e->t++;
    e->x += e->vx / TPS, e->y += e->vy / TPS;
    e->vy -= 18.0f / TPS;
    e->x += in->mx * 0.8f / TPS * 1.0f;
    if (e->atk == A_JKICK) {
      do_hits(e);
    } else if ((in->atk || in->kick) && e->y > 0.4f) {
      e->atk = A_JKICK, e->hitmask = 0;
      char_restart(e, "jumpkick", 0.04f);
    }
    if (e->y <= 0 && e->vy < 0) {
      e->y = 0;
      e->atk = A_NONE;
      e->st = S_IDLE, e->t = 0;
      fx_dust(e->x, e->z, 0.8f);
    }
    break;
  }
  case S_ATK: {
    const atk_data *ad = &ATK[e->atk];
    e->t++;
    if (in->atk || in->kick) e->chain_ok = 1;
    if (e->t < ad->a1 && ad->lunge > 0) e->x += (float)e->face * ad->lunge * 5.0f / TPS;
    if (e->def == D_DOLLY && e->atk == A_SPIN && e->t < ad->a1) e->x += (float)e->face * 3.4f / TPS; /* the clothesline runs */
    do_hits(e);
    if (e->t == ad->a0) sfx(SFX_WHIFF);
    bool chain = e->chain_ok && e->t >= ad->a1 && (e->atk == A_PUNCH1 || e->atk == A_PUNCH2) && !e->weapon;
    if (chain) {
      akind k = e->atk == A_PUNCH1 ? A_PUNCH2 : A_FINISH;
      start_attack(e, k);
      e->combo = k == A_PUNCH2 ? 2 : 0;
      break;
    }
    if (e->t >= ad->total) {
      e->st = S_IDLE, e->t = 0;
      if (e->atk == A_PUNCH1 || e->atk == A_PUNCH2) e->combo_t = 14; /* the next press within 14 ticks after the swing ends continues the chain (presses during the swing are buffered) */
      e->atk = A_NONE;
    }
    break;
  }
  case S_GRAB: {
    ent *v = e->victim < 0 ? NULL : &W.e[e->victim - MAXP];
    if (!v || !alive(v) || v->st != S_GRABBED) { e->st = S_IDLE; e->victim = -1; break; }
    e->t++;
    if (e->t > 240) { /* let go */
      v->st = S_IDLE, v->holder = -1, v->cd = 30, e->victim = -1, e->st = S_IDLE;
      break;
    }
    float back = -in->mx * (float)e->face;
    if ((in->grab || in->kick) && back > 0.5f) { /* throw behind: it flies over the shoulder */
      e->st = S_THROW, e->t = 0, e->throwfwd = 0;
      char_restart(e, "throw", 0.04f);
      sfx(SFX_THROW);
      break;
    }
    if (in->atk) {
      e->knees++;
      e->atk = A_KNEE;
      e->t = 0;
      e->hitmask = 0;
      char_restart(e, "knee", 0.04f);
      int vi = e->victim;
      ent_hurt(v, e, 6 * DEFS[e->def].dmg, 0, 0);
      /* the knee lands on a body still held: ent_hurt let go of the pair (release_pair, as for any blow on a held body),
         which ended every grab at the first knee, so three knees never threw anyone (nor did the bots) */
      if (alive(v) && !g_legacy_throw) { v->st = S_GRABBED; v->holder = pi; e->victim = vi; e->st = S_GRAB; char_restart(v, "grabbed", 0.0f); }
      else if (alive(v)) { v->st = S_GRABBED; v->holder = pi; char_restart(v, "grabbed", 0.0f); }
      e->hold = 4;
      if (e->knees >= 3) { e->st = S_THROW, e->t = 0, e->throwfwd = 1; char_restart(e, "throw", 0.04f); sfx(SFX_THROW); if (g_legacy_grab) e->face = -e->face; }
    }
    break;
  }
  case S_THROW: {
    ent *v = e->victim < 0 ? NULL : &W.e[e->victim - MAXP];
    e->t++;
    if (v && e->t == 15 && v->st == S_GRABBED) {
      v->st = S_THROWN, v->holder = -1, v->t = 0, v->hitmask = 0, v->bowled = 0, v->thrower = pi;
      ent_hurt(v, e, 16 * DEFS[e->def].dmg, 0, 0);
      /* a throw that kills still flies: the body is thrown, lands and lies (it fell where it stood before, so a lethal throw,
         the usual one after three knees, never bowled anyone over) */
      if (!g_legacy_throw || v->hp > 0) v->st = S_THROWN, v->t = 0, v->hitmask = 0;
      if (g_legacy_grab) v->vx = -(float)e->face * 9.0f, v->vy = 5.0f, v->y = 0.2f;
      else v->vx = (float)e->face * (e->throwfwd ? 1.0f : -1.0f) * (e->def == D_DOLLY ? 11.5f : 9.0f), v->vy = 2.5f; /* from up there */
      char_restart(v, "thrown", 0.02f);
      v->invuln = 0;
      e->victim = -1;
    }
    if (e->t >= 42) { if (e->face * 1 != 0) {} e->st = S_IDLE, e->t = 0; e->victim = -1; }
    break;
  }
  default: e->st = S_IDLE; break;
  }
  for (int k = 0; k < W.players; k++) { /* fighters do not stand inside each other */
    const ent *o = &W.p[k];
    if (k == pi || !o->used || o->st == S_DEAD) continue;
    float dx = e->x - o->x, dz = e->z - o->z, d2 = dx * dx + dz * dz;
    if (d2 < 0.5f) { float d = sqrtf(d2) + 0.001f; e->x += dx / d * 0.03f; e->z += dz / d * 0.03f; if (d2 < 0.0001f) e->z += (pi > k ? 0.03f : -0.03f); } /* about 0.7 m apart: the bodies are broad now */
  }
  arena_clamp(e);
}

/* ── the jukebox (the docks, Arthur's arena) ─────────────────────────────
 * From his second phase Arthur may walk to the jukebox on the pavement, raise his fist (36 ticks: the tell, while its
 * lights flicker and a ring glows at its foot), and thump it: his theme starts, he gets quicker for six seconds and a
 * shockwave runs out along the floor from it. A fighter on the ground when the ring passes is knocked down; one in the
 * air (a jump) is not. The ring grows the way Shockwave3D does (quick, then slowing), so the bent picture and the rule
 * agree. Then he turns to the fighters with a thumbs up: an opening. */
static bool juke_ready(const ent *e) {
  return W.juke.here && W.juke.cd == 0 && W.juke.t == 0 && e->phase >= 1 && on_screen(JUKE_X + 0.72f, JUKE_Z + 0.62f, 0.5f) && on_screen(JUKE_X, JUKE_Z + 0.3f, 0.3f);
}
static void juke_fire(ent *e) {
  W.juke.on = true, W.juke.t = 1, W.juke.r = W.juke.r0 = 0.3f, W.juke.mask = 0;
  W.juke.owner = e->idx, W.juke.fired++, W.juke.uses++, W.juke.cd = 900;
  e->buff = 360;
  W.shake = 12, W.shake_amp = 0.1f;
  rumble(-1, 0.9f, 0.3f, 350); /* the jukebox blast: heavy and low */
  sfx(SFX_THUMP);
  fx_dust(JUKE_X, JUKE_Z + 0.5f, 2.2f);
  fx_flash(JUKE_X, 1.2f, JUKE_Z + 0.4f, 1, 0.55f, 0.25f, 10);
}
static void juke_tick(void) {
  if (W.juke.cd > 0) W.juke.cd--;
  if (W.juke.t <= 0) return;
  float u = (float)W.juke.t / JUKE_TICKS;
  W.juke.r0 = W.juke.r;
  W.juke.r = JUKE_RMAX * (1 - (1 - u) * (1 - u));
  ent *a = W.juke.owner >= 0 && W.e[W.juke.owner].used ? &W.e[W.juke.owner] : NULL;
  for (int i = 0; i < MAXP; i++) {
    ent *p = &W.p[i];
    if (!alive(p) || (W.juke.mask & (1u << i))) continue;
    float dx = p->x - JUKE_X, dz = p->z - JUKE_Z;
    if (dx * dx + dz * dz > (W.juke.r + 0.35f) * (W.juke.r + 0.35f)) continue; /* not reached yet */
    W.juke.mask |= 1u << i;
    bool ground = p->y < 0.3f && p->invuln == 0 && !(p->st == S_FALL || p->st == S_DOWN || p->st == S_GETUP || p->st == S_THROWN || p->st == S_GRABBED || p->st == S_DEAD);
    if (!ground) { W.juke.dodges++; continue; } /* jumped it (or was down or blinking already) */
    float src[2] = { p->x, p->z }; /* the ring is where the fighter stands, on screen */
    g_src = src;
    ent_hurt(p, a, 9.0f * 0.8f * DEFS[D_ARTHUR].dmg, 2, 0);
    g_src = NULL;
    W.juke.hits++;
  }
  if (++W.juke.t > JUKE_TICKS) W.juke.t = 0;
}

/* ── enemies ────────────────────────────────────────────────────────── */
static int nearest_player(const ent *e) {
  int best = -1;
  float bd = 1e9f;
  for (int i = 0; i < MAXP; i++) {
    const ent *p = &W.p[i];
    if (!p->used || p->st == S_DEAD || p->hp <= 0) continue;
    float d = (p->x - e->x) * (p->x - e->x) + (p->z - e->z) * (p->z - e->z);
    if (d < bd) bd = d, best = i;
  }
  return best;
}


static void enemy_pick_attack(ent *e) {
  const char_def *d = &DEFS[e->def];
  switch (e->def) {
  case D_THUG: start_attack(e, ri(0, 3) ? A_PUNCH1 : A_KICK); break;
  case D_PUNK: start_attack(e, ri(0, 1) ? A_KICK : A_PUNCH2); break;
  case D_RUNNER: start_attack(e, A_CHARGE); e->vx = (float)e->face * 7.0f; break;
  case D_KNIFER: e->weapon = 3; weapon_show(e); start_attack(e, A_STAB); break;
  case D_BRUISER: start_attack(e, ri(0, 2) ? A_SWING : A_SLAM); break;
  case D_THROWER: start_attack(e, A_THROW); break;
  case D_GUARD: start_attack(e, A_BASH); break;
  case D_WANKER: start_attack(e, ri(0, 4) < 3 ? A_HAYMAKER : ri(0, 1) ? A_PUNCH1 : A_KICK); break;
  case D_CREEP: start_attack(e, ri(0, 2) ? A_FLASH : A_PUNCH1); break;
  default:
    if (d->boss || e->def == D_RINGMASTER) { /* three phases: each adds a move and a bigger tell; the last one charges along the lane */
      static const akind MOVES[5][3][3] = {
        { { A_SWING, A_PUNCH2, A_PUNCH2 }, { A_SWING, A_SLAM, A_PUNCH2 }, { A_SLAM, A_RUSH, A_SWING } }, /* foreman */
        { { A_KICK, A_STAB, A_KICK }, { A_BSPIN, A_STAB, A_KICK }, { A_BSPIN, A_RUSH, A_STAB } },        /* matron */
        { { A_SWING, A_PUNCH2, A_SWING }, { A_SLAM, A_SWING, A_PUNCH2 }, { A_RUSH, A_SLAM, A_BSPIN } },  /* ringmaster */
        { { A_SWING, A_SLAM, A_SWING }, { A_BSPIN, A_SLAM, A_SWING }, { A_RUSH, A_BSPIN, A_SLAM } },    /* colossus */
        { { A_PUNCH2, A_FLICK, A_PUNCH1 }, { A_FLICK, A_RUSH, A_PUNCH2 }, { A_RUSH, A_FLICK, A_PUNCH2 } }, /* arthur: jabs, the jacket flick, a shoulder charge */
      };
      if (e->def == D_ARTHUR && juke_ready(e) && (W.juke.uses == 0 || ri(0, 2) == 0)) { e->juke = 1; return; } /* off to thump the jukebox (the first time he can, always) */
      int ph = d->boss ? (e->phase > 2 ? 2 : e->phase) : (e->hp < e->maxhp * 0.5f ? 1 : 0); /* the mini boss: two of its rows by health */
      akind k = MOVES[e->def - D_FOREMAN][ph][ri(0, 2)];
      if (k == A_STAB) { e->weapon = 3; weapon_show(e); }
      start_attack(e, k);
      if (k == A_RUSH) e->vx = (float)e->face * (7.5f + (float)W.stage * 0.5f);
    } else start_attack(e, A_PUNCH1);
  }
  (void)d;
}

/* boss phases: below two thirds and one third of its health it stops, roars, calls help and comes back with more.
   Checked whenever it is on its feet or lying down, so a beating on the ground cannot skip a phase (ent_hurt also keeps
   its health above the next threshold until that phase has begun). */
static void boss_phase(ent *e) {
  if (!DEFS[e->def].boss || !alive(e) || e->phase >= 2) return;
  if (!(e->st == S_IDLE || e->st == S_WALK || e->st == S_DOWN || e->st == S_GETUP || e->st == S_HIT || (e->st == S_ATK && e->t < 0))) return;
  int want = e->hp < e->maxhp * 0.34f ? 2 : e->hp < e->maxhp * 0.67f ? 1 : 0;
  if (want <= e->phase) return;
  e->phase++;
  e->st = S_TAUNT; e->t = 0; e->invuln = 90; e->atk = A_NONE; e->y = 0;
  char_restart(e, e->def == D_ARTHUR && e->phase == 2 ? "thumbsup" : taunt_clip(e), 0.05f);
  if (e->def == D_ARTHUR && e->phase == 2) sfx(SFX_SWAGGER);
  if (e->token) { e->token = 0; W.tokens_out--; }
  W.phase_banner = 150, W.shake = 24, W.shake_amp = 0.1f;
  rumble(-1, 0.6f, 0.2f, 300);
  sfx(SFX_SLAM);
  fx_flash(e->x, 1.5f, e->z, 1, 0.3f, 0.2f, 18);
  fx_dust(e->x, e->z, 2.5f);
  if (e->phase > W.st.boss_phase) W.st.boss_phase = e->phase;
  static const int ADDS[5][2][2] = { { { D_THUG, D_THUG }, { D_THROWER, D_GUARD } }, { { D_KNIFER, D_PUNK }, { D_THROWER, D_RUNNER } },
                                     { { D_PUNK, D_RUNNER }, { D_GUARD, D_THROWER } }, { { D_GUARD, D_KNIFER }, { D_THROWER, D_RUNNER } },
                                     { { D_PUNK, D_RUNNER }, { D_THUG, D_KNIFER } } };
  for (int k = 0; k < 2; k++) ent_spawn_enemy(ADDS[e->def - D_FOREMAN][e->phase - 1][k], W.cam_x + (k ? 9.0f : -9.0f), rf(-2, 2));
}

static void enemy_update(int ei) {
  ent *e = &W.e[ei];
  if (!e->used) return;
  apply_invuln(e);
  if (e->hold > 0) { e->hold--; return; }
  if (e->cd > 0) e->cd--;
  if (e->buff > 0) e->buff--;
  if (e->react > 0) e->react--;
  boss_phase(e);
  if (common_state(e)) {
    if (e->st == S_DEAD && e->t > 70) {
      e->fade = 1;
    }
    arena_clamp(e);
    return;
  }
  const char_def *d = &DEFS[e->def];
  bool boss = d->boss;
  if (g_freeze && (e->st == S_IDLE || e->st == S_WALK)) { e->face = -1; e->st = S_IDLE; char_play(e, e->react > 6 && temper_of(e->def) != FACE_PLAIN ? (temper_of(e->def) == FACE_FEAR ? "react_fear" : temper_of(e->def) == FACE_ANGER ? "react_rage" : "react_bliss") : idle_clip(e), 0.2f); if (e->agent && crowd.id) cw_crowd_set_target(crowd, AG(e->agent), (t3_vec3){ e->x, 0, e->z }); arena_clamp(e); return; }
  if (on_screen(e->x, e->z, 0.3f) && e->ptimer < 1000) e->ptimer++; /* ticks inside the picture: no attack before 20 */
  switch (e->st) {
  case S_ATK: {
    const atk_data *ad = &ATK[e->atk];
    e->t++;
    if (e->t == 0 && e->atk != A_JUKE && e->atk != A_HAYMAKER && e->atk != A_FLASH) char_restart(e, atk_clip(e, e->atk), 0.05f);
    if (e->t == 0 && e->atk == A_JUKE) juke_fire(e);
    if (e->t < 0) { e->hurtflash = 0.6f; break; }
    if (e->atk == A_CHARGE || e->atk == A_RUSH || e->atk == A_BARGE) {
      e->x += e->vx / TPS;
      if (e->t > ad->a1) e->vx *= 0.8f;
    } else if (e->t < ad->a1 && ad->lunge > 0) e->x += (float)e->face * ad->lunge * 4.0f / TPS;
    if (e->atk == A_THROW && e->t == ad->a0) proj_throw(e);
    do_hits(e);
    if (e->t >= ad->total && e->atk == A_JUKE) { /* the swagger: a thumbs up while his theme plays (an opening for the fighters) */
      e->st = S_TAUNT, e->t = 0, e->atk = A_NONE;
      if (e->token) { e->token = 0; W.tokens_out--; }
      char_restart(e, "thumbsup", 0.12f);
      sfx(SFX_SWAGGER);
      break;
    }
    if (e->t >= ad->total) {
      e->st = S_IDLE, e->t = 0, e->atk = A_NONE;
      if (e->token) { e->token = 0; W.tokens_out--; }
      e->ranged = 0;
      e->cd = boss ? (e->phase >= 2 ? 25 + ri(0, 30) : 40 + ri(0, 40)) : e->def == D_THROWER ? 110 + ri(0, 90) : 70 + ri(0, 90);
      if (e->buff > 0) e->cd /= 2; /* his theme is playing: he comes back at you quicker */
      if (e->def == D_THUG && ri(0, 3) == 0) { e->cd = 20; } /* a quick second swing now and then */
      if (e->def == D_KNIFER || (boss && e->weapon == 3)) { e->weapon = e->def == D_KNIFER ? 3 : 0; weapon_show(e); }
    }
    break;
  }
  default: {
    if (e->juke) { /* on the way to the jukebox: stand at its right front corner, facing it, then the thump */
      const float sx = JUKE_X + 0.72f, sz = JUKE_Z + 0.62f;
      if (++e->juke > 240 || !juke_ready(e)) { e->juke = 0; e->cd = 40; }
      else {
        float jx = sx - e->x, jz = sz - e->z, jd = sqrtf(jx * jx + jz * jz);
        if (jd > 0.05f) { /* straight there, with purpose (not through the crowd's avoidance, which wandered) */
          float step = 4.0f / TPS < jd ? 4.0f / TPS : jd;
          e->x += jx / jd * step, e->z += jz / jd * step;
          if (crowd.id && e->agent) { cw_crowd_clear_target(crowd, AG(e->agent)); cw_crowd_set_position(crowd, AG(e->agent), (t3_vec3){ e->x, 0, e->z }); cw_crowd_set_velocity(crowd, AG(e->agent), (t3_vec3){ 0, 0, 0 }); }
        }
        jx = sx - e->x, jz = sz - e->z;
        if (jx * jx + jz * jz < 0.05f) {
          if (crowd.id && e->agent) cw_crowd_clear_target(crowd, AG(e->agent));
          e->face = -1, e->juke = 0;
          start_attack(e, A_JUKE);
          break;
        }
        e->face = jx >= 0 ? 1 : -1;
        e->st = S_WALK;
        char_play(e, "run", 0.15f);
        break;
      }
    }
    int ti = nearest_player(e);
    if (ti < 0) { e->st = S_IDLE; e->ranged = 0; char_play(e, idle_clip(e), 0.2f); if (e->agent && crowd.id) cw_crowd_set_velocity(crowd, AG(e->agent), (t3_vec3){ 0, 0, 0 }); break; }
    ent *p = &W.p[ti];
    float side = (e->x < p->x) ? -1.0f : 1.0f; /* the side it is already on */
    float dx = p->x - e->x, dz = p->z - e->z;
    float reach = boss ? 1.3f : 0.95f;
    bool ready = e->ptimer >= 20 && on_screen(e->x, e->z, 0.3f); /* walked into the picture: may attack */
    if (g_no_screen_rule) ready = true;
    const char *clip = NULL;
    float tx, tz;
    if (e->def == D_THROWER) {
      /* the thrower keeps its distance, lines up with its fighter's lane and throws; one thrower per fighter at a time */
      int busy = 0;
      for (int k = 0; k < MAXE; k++) if (W.e[k].used && W.e[k].ranged && W.e[k].target == ti && k != ei) busy++;
      if (!e->ranged && e->cd == 0 && busy == 0 && ready) { e->ranged = 1; e->target = ti; }
      float want_d = absf(dx) < 2.2f ? 5.0f : 4.4f;
      float sn, cs;
      e->orbit += 0.035f;
      cw_sincos(e->orbit, &sn, &cs);
      tz = e->ranged ? p->z : p->z + (e->idx & 1 ? 1.2f : -1.2f) + 0.5f * sn; /* waiting: it paces across the street */
      want_d += 0.4f * cs;
      tx = p->x + side * want_d;
      if (!on_screen(tx, tz, 0.8f)) tx = p->x - side * want_d; /* no room on this side of the picture: go round */
      if (e->ranged && e->cd == 0 && absf(dz) < 0.3f && absf(dx) > 2.4f && absf(dx) < 7.0f && ready) {
        if (e->agent && crowd.id) cw_crowd_clear_target(crowd, AG(e->agent));
        e->face = dx >= 0 ? 1 : -1;
        enemy_pick_attack(e);
        break;
      }
    } else {
      /* token: one request per think */
      int held = 0; /* attackers already on this target: two at most per fighter */
      for (int k = 0; k < MAXE; k++) if (W.e[k].used && W.e[k].token && W.e[k].target == ti && !DEFS[W.e[k].def].boss) held++;
      bool want = !e->token && e->cd == 0 && (boss || held < 2) && ready;
      float dist = sqrtf(dx * dx + dz * dz);
      if (want && dist < 5.0f) { e->token = 1; e->target = ti; W.tokens_out++; e->mood = 0, e->side_pref = 0; }
      if (e->token) {
        /* a second attacker on this fighter takes the other flank instead of standing inside the first */
        for (int k = 0; k < ei; k++) { const ent *o = &W.e[k]; if (o->used && o->token && o->target == ti && ((o->x < p->x) == (e->x < p->x))) { side = -side; break; } }
        tx = p->x + side * reach * 0.9f;
        tz = p->z;
        if (!on_screen(tx, tz, 0.7f)) tx = p->x - side * reach * 0.9f; /* no room on that side of the picture: the other flank */
      } else {
        /* waiting: a ring slot around the fighter, and something to do in it (BR4: nobody stands frozen) */
        if (e->still > 30) { /* blocked where it stands: circle, or if circling is blocked too, go round to the other side */
          if (e->mood != 1) e->mood = 1, e->mood_t = ri(120, 200), e->side_pref = 0, e->orbit += 1.5f;
          else e->mood = 4, e->mood_t = 150, e->side_pref = (int)-side;
        }
        if (--e->mood_t <= 0) {
          int r = ri(0, 9);
          e->mood = r < 4 ? 1 : r < 6 ? 2 : r < 7 ? 3 : r < 8 ? 4 : 0;
          if (e->def == D_CREEP && r >= 4) e->mood = 3; /* the creep beckons: come on, hit me */
          e->mood_t = e->mood == 1 ? ri(120, 200) : e->mood == 2 ? 56 : e->mood == 3 ? 72 : e->mood == 4 ? 150 : ri(90, 150);
          e->side_pref = e->mood == 4 ? (int)-side : 0;
          if (e->mood == 1 && ri(0, 1)) e->orbit += 3.14159f;
        }
        if (e->side_pref) side = (float)e->side_pref;
        static const float SLOT[8][2] = { { 2.2f, 0 }, { 2.45f, 1.05f }, { 2.45f, -1.05f }, { 3.25f, 0.5f }, { 3.25f, -0.5f }, { 3.6f, 1.55f }, { 3.6f, -1.55f }, { 4.3f, 0 } };
        int rank = 0;
        for (int k = 0; k < ei; k++) {
          const ent *o = &W.e[k];
          if (!o->used || o->token || o->st == S_DEAD || DEFS[o->def].boss || o->def == D_THROWER || (o->x < p->x) != (side < 0)) continue;
          if (nearest_player(o) == ti) rank++;
        }
        const float *sl = SLOT[rank & 7];
        float r0 = sl[0] + (float)(rank >> 3) * 0.8f, oz = sl[1];
        float sn, cs;
        switch (e->mood) {
        case 1: /* circle: a slow loop around the slot */
          e->orbit += 0.03f;
          cw_sincos(e->orbit, &sn, &cs);
          r0 += 0.55f * cs, oz += 1.0f * sn;
          break;
        case 2: { /* feint: step in, show the guard pose, step back out */
          int t = 56 - e->mood_t;
          if (t < 22) r0 = 1.5f;
          else if (t < 34) { r0 = 1.5f; clip = "block"; }
          break;
        }
        case 3: clip = e->def == D_ARTHUR && ((W.tick / 600) & 1) ? "thumbsup" : taunt_clip(e); break; /* beckon (Arthur combs his hair or gives a thumbs up) */
        default: /* sway: a step forward and back and across */
          e->orbit += 0.04f;
          cw_sincos(e->orbit, &sn, &cs);
          r0 += 0.3f * sn, oz += 0.45f * cs;
          break;
        }
        tx = p->x + side * r0;
        tz = p->z + oz;
        if (!on_screen(tx, tz, 0.7f) && on_screen(p->x - side * r0, tz, 0.7f)) tx = p->x - side * r0; /* that side is off camera */
        if (tz < -LANE_Z + 0.2f || tz > LANE_Z - 0.2f) tz = p->z - oz; /* mirrored when the lane edge is in the way */
        if (e->side_pref && (e->x < p->x) == (side < 0) && absf(e->x - tx) < 0.6f) e->side_pref = 0, e->mood_t = 1; /* got round */
      }
    }
    tz = clampf(tz, -LANE_Z + 0.2f, LANE_Z - 0.2f);
    { /* every place an enemy waits or fights is inside the picture (the near lane sees far less street than the far one):
         a slot off camera left an enemy waiting where nobody could reach it and it could never be allowed to attack */
      float hw = view_halfw(tz, 1.0f) - 0.7f;
      tx = clampf(tx, W.cam_x - hw, W.cam_x + hw);
    }
    if (crowd.id && e->agent) {
      e->thinkt++;
      if (e->thinkt % 6 == 0) cw_crowd_set_target(crowd, AG(e->agent), (t3_vec3){ tx, 0, tz });
    }
    float to_x = tx - e->x, to_z = tz - e->z;
    bool arrived = to_x * to_x + to_z * to_z < 0.05f;
    e->face = dx >= 0 ? 1 : -1;
    e->st = arrived ? S_IDLE : S_WALK;
    if (e->react > 6) { /* the reaction plays on past the flinch (render only: the crowd still moves the body) */
      int tp = temper_of(e->def);
      if (tp != FACE_PLAIN) clip = tp == FACE_FEAR ? "react_fear" : tp == FACE_ANGER ? "react_rage" : "react_bliss", arrived = true;
    }
    char_play(e, clip && arrived ? clip : clip && !strcmp(clip, "block") ? clip : arrived ? idle_clip(e) : e->def == D_CREEP ? "lurch" : (d->speed > 2.4f ? "run" : "walk"), 0.15f);
    if (e->token && absf(dx) < reach + 0.15f && absf(dz) < 0.45f && e->cd == 0 && ready) {
      if (e->agent && crowd.id) cw_crowd_clear_target(crowd, AG(e->agent));
      e->cd = 0;
      enemy_pick_attack(e);
    } else if (e->token && e->def == D_RUNNER && absf(dz) < 0.35f && absf(dx) > 3.0f && absf(dx) < 5.5f && ready) {
      enemy_pick_attack(e); /* the runner charges from a distance */
    } else if (e->token && e->def == D_WANKER && e->cd == 0 && absf(dz) < 0.35f && absf(dx) > 2.6f && absf(dx) < 5.0f && ready && ri(0, 59) == 0) {
      if (e->agent && crowd.id) cw_crowd_clear_target(crowd, AG(e->agent)); /* now and then he lowers a shoulder and barges along the lane */
      start_attack(e, A_BARGE);
      e->vx = (float)e->face * 6.0f;
    } else if (e->token && e->t++ > 600) { /* a token held too long is returned */
      e->token = 0; W.tokens_out--; e->cd = 60; e->t = 0;
    }
    break;
  }
  case S_TAUNT:
    if (++e->t > 84) { e->st = S_IDLE; e->t = 0; e->cd = 20; }
    break;
  }
  arena_clamp(e);
}

/* ── items ──────────────────────────────────────────────────────────── */
static void items_update(void) {
  for (int i = 0; i < MAXITEM; i++) {
    item_t *it = &W.item[i];
    if (!it->used) continue;
    it->t++;
    if (it->y > 0.05f && it->kind == 4) it->y = clampf(it->y - 0.03f, 0.05f, 9);
    else if (it->y > 0.05f) it->y = clampf(it->y - 0.05f, 0.05f, 9);
    /* reach: 0.8 m along the street, 1.0 m across it (a lane away); the nearest fighter gets it, the lower number on a tie */
    int who = -1;
    float best = 1;
    for (int p = 0; p < MAXP; p++) {
      ent *e = &W.p[p];
      if (!e->used || e->st == S_DEAD || e->st == S_FALL || e->st == S_DOWN || e->st == S_THROWN || e->st == S_GRABBED) continue;
      if (it->kind >= 1 && it->kind <= 3 && e->weapon) continue; /* one weapon at a time */
      float dx = e->x - it->x, dz = e->z - it->z, q = dx * dx / 0.64f + dz * dz / 1.0f;
      if (q < best) best = q, who = p;
    }
    if (who >= 0) {
      ent *e = &W.p[who];
      if (it->kind == 0) { e->hp = clampf(e->hp + e->maxhp * 0.3f, 0, e->maxhp); sfx(SFX_HEAL); fx_flash(it->x, 1, it->z, 0.3f, 1, 0.4f, 6); }
      else if (it->kind == 4) { e->score += 50; W.total_score += 50; W.coins++; sfx(SFX_PICK); }
      else { e->weapon = it->kind; e->wdur = it->kind == 3 ? 6 : 9; weapon_show(e); sfx(SFX_PICK); }
      it->used = false;
      W.st.pickups++;
    }
    if (it->used && it->kind != 4 && it->t > 60 * 40) it->used = false;
  }
}

/* ── the match ──────────────────────────────────────────────────────── */
static void free_all(void) {
  for (int i = 0; i < MAXP; i++) if (W.p[i].used) { char_free(&W.p[i]); W.p[i].used = false; }
  for (int i = 0; i < MAXE; i++) if (W.e[i].used) { char_free(&W.e[i]); W.e[i].used = false; }
}

void sim_put_away(void) { free_all(); }

void sim_reset(int players, int stage) {
  bool keep = stage > 0 && players == W.players;
  ent old[MAXP];
  memcpy(old, W.p, sizeof old);
  int total = W.total_score;
  if (crowd.id) { cw_crowd_free(crowd); crowd = (cw_crowd){ 0 }; }
  free_all();
  if (!keep) { memset(&W, 0, sizeof W); cw_rng_seed(&W.rng, 20260101 + g_seed_salt); }
  else { memset(W.e, 0, sizeof W.e); memset(W.item, 0, sizeof W.item); memset(W.prop, 0, sizeof W.prop); W.wave = 0; W.kills = 0; W.boss_active = false; W.combo_n = 0; W.tokens_out = 0; }
  for (int i = 0; i < 8; i++) W.proj[i].used = false;
  memset(&W.st, 0, sizeof W.st);
  W.st.first_hit = -1, W.st.boss_t0 = -1, W.st.boss_t1 = -1;
  W.st.t0 = W.tick, W.st.wave_t0 = W.tick;
  W.phase_banner = 0;
  W.players = players, W.stage = stage;
  W.total_score = keep ? total : 0;
  W.phase = PH_INTRO, W.phase_t = 0;
  W.cam_x = 0, W.slow = 0;
  memset(&W.juke, 0, sizeof W.juke);
  W.juke.here = (stage & 3) == 1, W.juke.owner = -1; /* the docks have the jukebox */
  level_build(stage);
  crowd_setup();
  for (int i = 0; i < players; i++) {
    int def = g_heroes[i];
    ent_init(&W.p[i], def, true, -3.0f + (float)i * 0.9f, -1.2f + (float)i * 0.8f);
    W.p[i].lives = keep ? old[i].lives : 3;
    W.p[i].score = keep ? old[i].score : 0;
    if (keep) { W.p[i].hp = W.p[i].maxhp * 0.5f + old[i].hp * 0.5f; if (W.p[i].hp > W.p[i].maxhp) W.p[i].hp = W.p[i].maxhp; if (old[i].hp <= 0) W.p[i].hp = W.p[i].maxhp * 0.6f; }
    W.p[i].idx = i;
    W.p[i].face = 1;
  }
  stage_wave_load(stage, 0);
}

/* debug: straight to a wave of this stage (the camera and the fighters jump to its lock) */
void sim_goto_wave(int wave) {
  for (int i = 0; i < MAXE; i++) if (W.e[i].used) ent_remove(&W.e[i]);
  W.tokens_out = 0, W.boss_active = false;
  W.wave = wave;
  stage_wave_load(W.stage, wave);
  W.cam_x = W.lock_x;
  for (int i = 0; i < W.players; i++) { W.p[i].x = W.cam_x - 2.0f + (float)i * 0.9f; if (W.p[i].agent && crowd.id) cw_crowd_set_position(crowd, AG(W.p[i].agent), (t3_vec3){ W.p[i].x, 0, W.p[i].z }); }
  W.phase = PH_PLAY, W.phase_t = 0;
  W.st.t0 = W.tick, W.st.wave_t0 = W.tick;
}

/* Bodies on their feet never stand inside each other, the crowd's walkers included: every pair closer than an ellipse
   1.0 m along the street and 0.62 m across it (larger for bosses) is pushed out to its edge, the lighter body moving
   more, a few passes a tick. A held body moves with its holder as one (the push goes to the holder and the held body
   follows), so a grab never lets a third fighter stand inside either of them. Bodies lying on the street keep apart
   from each other the same way in a longer ellipse (1.1 m by 0.64 m: a body lies along the street), so two knocked
   down thugs never lie in a heap inside each other; a fighter may still step over someone lying down, and bodies in
   the air are left alone. It used to be a gentle 4 cm nudge that skipped grabs and the fallen, and the crowd moved its
   walkers straight back: three fighters and a held thug drew as one blob. */
static float body_size(const ent *e) { return DEFS[e->def].boss ? 1.4f : DEFS[e->def].mini ? 1.2f : 1.0f; }
static ent *body_at(int idx) { return idx < MAXP ? &W.p[idx] : &W.e[idx - MAXP]; }
static void follow_holder(ent *v) { /* a held body sits in front of its holder (the S_GRABBED rule) */
  if (v->st != S_GRABBED || v->holder < 0) return;
  ent *h = body_at(v->holder);
  if (h->used && h->victim == ent_index(v)) v->x = h->x + (float)h->face * 0.75f, v->z = h->z;
}
int g_sep_overlaps; /* debug: pairs still inside each other after the last tick's separation */
static bool sep_pair(const ent *a, const ent *c, bool la, bool lc, float *ax, float *az) {
  if (la != lc) return false; /* standing next to someone lying down is fine */
  float s = body_size(a) + body_size(c);
  *ax = (la ? 0.55f : 0.5f) * s, *az = (la ? 0.32f : 0.31f) * s;
  return true;
}
static void separate_bodies(void) {
  ent *b[MAXP + MAXE], *root[MAXP + MAXE];
  bool lie[MAXP + MAXE];
  int n = 0;
  for (int i = 0; i < MAXP + MAXE; i++) {
    ent *e = body_at(i);
    if (!e->used || e->st == S_DEAD || e->st == S_THROWN || e->y > 0.3f) continue;
    lie[n] = e->st == S_DOWN || e->st == S_FALL;
    b[n] = root[n] = e;
    if (e->st == S_GRABBED && e->holder >= 0) {
      ent *h = body_at(e->holder);
      if (h->used && h->victim == i) root[n] = h;
    }
    n++;
  }
  for (int pass = 0; pass < 4; pass++) {
    bool moved = false;
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) {
      float ax, az;
      if (root[i] == root[j] || !sep_pair(b[i], b[j], lie[i], lie[j], &ax, &az)) continue; /* a holder and the body it holds */
      float dx = b[j]->x - b[i]->x, dz = b[j]->z - b[i]->z, q = dx * dx / (ax * ax) + dz * dz / (az * az);
      if (q >= 0.999f) continue;
      if (q < 1e-6f) { dx = 0, dz = ((i + j) & 1) ? 0.01f : -0.01f; q = dz * dz / (az * az); } /* one on top of the other: split across the street */
      float k = 1.0f / sqrtf(q) - 1.0f, cx = dx * k, cz = dz * k;
      float mi = body_size(root[i]), mj = body_size(root[j]), fi = mj / (mi + mj);
      root[i]->x -= cx * fi, root[i]->z -= cz * fi;
      root[j]->x += cx * (1 - fi), root[j]->z += cz * (1 - fi);
      arena_clamp(root[i]), arena_clamp(root[j]);
      { /* held at the edge of the picture (the clamp undid the push along the street): step apart across it instead */
        float ex = b[j]->x - b[i]->x, ez = b[j]->z - b[i]->z;
        if (ex * ex / (ax * ax) + ez * ez / (az * az) < 0.98f && ex * ex < ax * ax) {
          float want = az * sqrtf(1.0f - ex * ex / (ax * ax)), sgn = ez > 0 ? 1.0f : ez < 0 ? -1.0f : (((i + j) & 1) ? 1.0f : -1.0f);
          float dzz = sgn * want - ez;
          root[i]->z -= dzz * fi, root[j]->z += dzz * (1 - fi);
          arena_clamp(root[i]), arena_clamp(root[j]);
        }
      }
      for (int k2 = 0; k2 < n; k2++) if (root[k2] != b[k2]) follow_holder(b[k2]);
      moved = true;
    }
    if (!moved) break;
  }
  /* the crowd steers from where the body now stands */
  if (crowd.id)
    for (int i = 0; i < n; i++)
      if (!b[i]->player && b[i]->agent) cw_crowd_set_position(crowd, AG(b[i]->agent), (t3_vec3){ b[i]->x, 0, b[i]->z });
  g_sep_overlaps = 0;
  for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) {
    float ax, az;
    if (root[i] == root[j] || !sep_pair(b[i], b[j], lie[i], lie[j], &ax, &az)) continue;
    ax *= 0.9f, az *= 0.9f; /* 10% slack: the clamp at the screen edge may win */
    float dx = b[j]->x - b[i]->x, dz = b[j]->z - b[i]->z;
    if (dx * dx / (ax * ax) + dz * dz / (az * az) < 1) g_sep_overlaps++;
  }
}

static void spawn_from_queue(void) {
  for (int i = 0; i < W.spawn_q_n; i++) {
    if (W.spawn_q[i].def < 0) continue;
    if (!W.wave_spawned) continue;
    if (W.spawn_q[i].delay > 0) { W.spawn_q[i].delay--; continue; }
    if (enemies_alive() >= 6 + 2 * W.players && !DEFS[W.spawn_q[i].def].boss) continue;
    float side = (float)W.spawn_q[i].side;
    float x = W.cam_x + side * (level_cam_halfw() + 1.4f);
    ent_spawn_enemy(W.spawn_q[i].def, x, rf(-LANE_Z + 0.3f, LANE_Z - 0.3f));
    W.spawn_q[i].def = -1;
  }
}

void sim_tick(const inputs *in) {
  W.frames++;
  if (W.phase == PH_INTRO) { if (++W.phase_t > 150) { W.phase = PH_PLAY; W.phase_t = 0; } }
  else if (W.phase == PH_CLEAR || W.phase == PH_OVER || W.phase == PH_WIN) W.phase_t++;
  if (W.slow > 0) { W.slow--; if (W.slow & 1) return; } /* a boss falls in slow motion */
  if (W.hitstop > 0) {
    W.hitstop--;
    for (int i = 0; i < MAXP; i++) if (W.p[i].used && W.p[i].hold == 0) { /* hold the pose */ }
    return;
  }
  W.tick++;
  if (W.shake > 0) W.shake--;
  if (W.combo_t > 0 && --W.combo_t == 0) W.combo_n = 0;
  if (W.go_flash > 0) W.go_flash--;
  if (W.phase_banner > 0) W.phase_banner--;

  /* positions into the crowd: bodies not driven by it */
  if (crowd.id) {
    for (int i = 0; i < MAXP; i++) if (W.p[i].used && W.p[i].agent) cw_crowd_set_position(crowd, AG(W.p[i].agent), (t3_vec3){ W.p[i].x, 0, W.p[i].z });
    for (int i = 0; i < MAXE; i++) {
      ent *e = &W.e[i];
      if (e->used && e->agent && !(e->st == S_WALK || e->st == S_IDLE)) cw_crowd_set_position(crowd, AG(e->agent), (t3_vec3){ e->x, 0, e->z });
    }
  }
  bool play = W.phase == PH_PLAY;
  for (int i = 0; i < W.players; i++) {
    inputs z = { 0 };
    player_update(i, play ? &in[i] : &z);
  }
  if (play) {
    spawn_from_queue();
    for (int i = 0; i < MAXE; i++) enemy_update(i);
  } else {
    for (int i = 0; i < MAXE; i++) if (W.e[i].used) { enemy_update(i); }
  }
  if (crowd.id) {
    cw_crowd_step(crowd, 1.0f / TPS);
    for (int i = 0; i < MAXE; i++) {
      ent *e = &W.e[i];
      if (e->used && e->agent && (e->st == S_WALK || e->st == S_IDLE)) {
        t3_vec3 p = cw_crowd_position(crowd, AG(e->agent));
        e->x = p.x, e->z = p.z;
      }
    }
  }
  for (int i = 0; i < MAXE; i++) if (W.e[i].used && W.e[i].fade) ent_remove(&W.e[i]);
  proj_update();
  juke_tick();
  separate_bodies();
  items_update();

  /* waves */
  if (play) {
    bool queue_left = false;
    for (int i = 0; i < W.spawn_q_n; i++) if (W.spawn_q[i].def >= 0) queue_left = true;
    if (!queue_left && enemies_alive() == 0 && W.wave_spawned) {
      int waves = stage_wave_count(W.stage);
      { int d = W.tick - W.st.wave_t0; W.st.waves_done++, W.st.wave_sum += d; if (d > W.st.wave_max) W.st.wave_max = d; W.st.wave_t0 = W.tick; }
      if (W.wave + 1 >= waves) stats_log("clear");
      if (W.wave + 1 >= waves) {
        W.phase = PH_CLEAR, W.phase_t = 0;
      } else {
        W.wave++;
        stage_wave_load(W.stage, W.wave);
        W.go_flash = 240;
        sfx(SFX_GO);
      }
    }
    /* everyone out of lives and down: game over */
    bool anyone = false;
    for (int i = 0; i < W.players; i++) if (W.p[i].used && (W.p[i].st != S_DEAD || W.p[i].lives > 0)) anyone = true;
    if (!anyone) { W.phase = PH_OVER, W.phase_t = 0; stats_log("over"); }
  }
  level_camera(1.0f / TPS);
  level_update();
  fx_tick();

  /* audits: positions are numbers, waiting enemies are never frozen, the match keeps moving */
  {
    bool progress = false;
    static float last_cam;
    static int last_kills, last_wave;
    int ehp = 0;
    for (int i = 0; i < MAXP + MAXE; i++) {
      ent *e = i < MAXP ? &W.p[i] : &W.e[i - MAXP];
      if (!e->used) continue;
      if (!isfinite(e->x) || !isfinite(e->y) || !isfinite(e->z) || !isfinite(e->hp)) { W.st.nan++; e->x = W.cam_x, e->y = 0, e->z = 0; }
      if (i >= MAXP) ehp += (int)e->hp;
      if (i < MAXP || !play || DEFS[e->def].boss || e->st == S_DEAD || g_freeze) continue;
      bool waiting = (e->st == S_IDLE || e->st == S_WALK) && !e->token && nearest_player(e) >= 0;
      float mv = absf(e->x - e->lx) + absf(e->z - e->lz);
      e->lx = e->x, e->lz = e->z;
      if (!waiting || mv > 0.004f || (e->mood == 3 && e->def != D_THROWER) || !on_screen(e->x, e->z, 0)) e->still = 0;
      else if (++e->still == 60) { W.st.frozen++; if (g_log_frozen) cw_info("brawler-frozen %s mood=%d tick=%d x=%.2f z=%.2f", DEFS[e->def].name, e->mood, W.tick, e->x, e->z); }
      if (e->still > W.st.still_max) W.st.still_max = e->still;
    }
    static int last_ehp;
    if (W.cam_x != last_cam || W.kills != last_kills || W.wave != last_wave || ehp != last_ehp || !play) progress = true;
    last_cam = W.cam_x, last_kills = W.kills, last_wave = W.wave, last_ehp = ehp;
    W.st.stall = progress ? 0 : W.st.stall + 1;
    if (W.st.stall > W.st.stall_max) W.st.stall_max = W.st.stall;
    if (g_log_frozen && W.st.stall == 1800) { /* 30 s without progress: say who is where */
      cw_info("brawler-stall stage=%d wave=%d cam=%.2f lock=%.2f spawned=%d qn=%d", W.stage, W.wave, W.cam_x, W.lock_x, W.wave_spawned, W.spawn_q_n);
      for (int i = 0; i < W.players; i++) cw_info("  P%d x=%.2f z=%.2f st=%d hp=%.0f", i + 1, W.p[i].x, W.p[i].z, W.p[i].st, W.p[i].hp);
      for (int i = 0; i < MAXE; i++) if (W.e[i].used) cw_info("  E%d %s x=%.2f z=%.2f st=%d hp=%.0f tok=%d rng=%d cd=%d pt=%d mood=%d on=%d", i, DEFS[W.e[i].def].name, W.e[i].x, W.e[i].z, W.e[i].st, W.e[i].hp, W.e[i].token, W.e[i].ranged, W.e[i].cd, W.e[i].ptimer, W.e[i].mood, on_screen(W.e[i].x, W.e[i].z, 0.8f));
      for (int i = 0; i < W.spawn_q_n; i++) if (W.spawn_q[i].def >= 0) cw_info("  Q%d %s delay=%d", i, DEFS[W.spawn_q[i].def].name, W.spawn_q[i].delay);
    }
    if (W.boss_active && W.st.boss_t0 < 0) W.st.boss_t0 = W.tick, W.st.boss_f0 = W.frames;
  }
  /* the hash the gates compare */
  uint32_t h = W.hash;
  for (int i = 0; i < MAXP; i++) if (W.p[i].used) { int32_t q[4] = { (int32_t)(W.p[i].x * 1000), (int32_t)(W.p[i].z * 1000), (int32_t)W.p[i].hp, W.p[i].st }; const uint8_t *b = (const uint8_t *)q; for (size_t j = 0; j < sizeof q; j++) h = (h ^ b[j]) * 16777619u; }
  for (int i = 0; i < MAXE; i++) if (W.e[i].used) { int32_t q[4] = { (int32_t)(W.e[i].x * 1000), (int32_t)(W.e[i].z * 1000), (int32_t)W.e[i].hp, W.e[i].st }; const uint8_t *b = (const uint8_t *)q; for (size_t j = 0; j < sizeof q; j++) h = (h ^ b[j]) * 16777619u; }
  W.hash = h;
}

/* one line per stage for the measurement scripts (tools/measure.mjs): ticks are 1/60 s of game time */
void stats_log(const char *why) {
  const int *h = W.st.atk_hits;
  cw_info("brawler-stats %s stage=%d players=%d ticks=%d first_hit=%d deaths=%d hits=%d p1=%d p2=%d fin=%d kick=%d jkick=%d spin=%d weapon=%d "
          "blocks=%d pickups=%d unfair=%d still_max=%d frozen=%d nan=%d stall_max=%d boss_ticks=%d boss_phase=%d waves=%d wave_avg=%d wave_max=%d "
          "dmg_taken=%d run_deaths=%d leak=%d juke=%d juke_hits=%d juke_dodges=%d boss_frames=%d boss_hits=%d",
          why, W.stage, W.players, W.tick - W.st.t0, W.st.first_hit, W.st.deaths, W.st.hits, h[A_PUNCH1], h[A_PUNCH2], h[A_FINISH], h[A_KICK],
          h[A_JKICK], h[A_SPIN], h[A_SWING] + h[A_STAB], W.st.blocks, W.st.pickups, W.st.unfair, W.st.still_max, W.st.frozen, W.st.nan, W.st.stall_max,
          W.st.boss_t0 >= 0 && W.st.boss_t1 >= 0 ? W.st.boss_t1 - W.st.boss_t0 : -1, W.st.boss_phase, W.st.waves_done,
          W.st.waves_done ? W.st.wave_sum / W.st.waves_done : 0, W.st.wave_max, W.st.dmg_taken, W.run_deaths, token_leak(), W.juke.uses, W.juke.hits, W.juke.dodges,
          W.st.boss_f0 > 0 && W.st.boss_f1 > 0 ? W.st.boss_f1 - W.st.boss_f0 : -1, W.st.boss_hits);
}

/* ── node sync (render side) ────────────────────────────────────────── */
static t3_quat qmul4(t3_quat a, t3_quat b);
static t3_vec3 qrot4(t3_quat q, t3_vec3 v);
/* a bone of a character in the world (the root turns the model by its yaw; a hoisted body's tip is left out of the arms'
   target on purpose: the hands go to where the hips are, measured below) */
static t3_vec3 ent_bone_world(const ent *e, const char *bone) {
  t3_vec3 l = rig_bone_point(&RIGS[rig_of[ent_index(e)]].sr, bone, (t3_vec3){ 0, 0, 0 });
  float yaw = ent_yaw(e), c = cw_cos(yaw), s = cw_sin(yaw);
  return (t3_vec3){ e->x + c * l.x + s * l.z, e->y + l.y, e->z - s * l.x + c * l.z };
}
/* the grab (the 0.1.1 review: the held body sat inside the holder's arms): both hands take the collar, one on each of the
   held body's shoulders, the elbows out, by a two-bone solve over the clip. In the hoist they go under the body, either side
   of its hips. game.grab: frames measured, the farthest either wrist was from its grip (mm, after the first 8 ticks of the
   hold), the closest a wrist came to the held body's spine line (mm: a hand inside the chest is under about 90) */
int g_grabm[3] = { 0, 0, 1000000 };
/* Arthur's taunt: the wrist rides along the side of the pompadour, front to back, twice (the clip's arm moved near the head
   but never reached it, so the comb combed the air) */
extern bool g_legacy_hold;
int g_comb[2]; /* debug game.comb: frames mid stroke, the farthest the wrist was from its place on the hair (mm) */
static void comb_hand(ent *e, float yaw) {
  rig *r = &RIGS[rig_of[ent_index(e)]];
  if (!r->ik[1].id) return;
  bool comb = e->clipname && !strcmp(e->clipname, "comb") && e->anim.id && !g_legacy_hold;
  cw_node_set(r->ik[1], "active", cw_val_bool(comb));
  if (!comb) return;
  float p = (float)cw_anim_player_position(e->anim), env = cw_clampf(p / 0.25f, 0, 1) * (1 - cw_clampf((p - 1.2f) / 0.3f, 0, 1));
  /* the strokes: 0.25..0.55 and 0.7..1.0 go back; between them the hand returns to the front */
  float st = p < 0.55f ? (p - 0.25f) / 0.3f : p < 0.7f ? 1 - (p - 0.55f) / 0.15f : (p - 0.7f) / 0.3f;
  st = cw_clampf(st, 0, 1), st = st * st * (3 - 2 * st);
  t3_vec3 l = rig_bone_point(&r->sr, "Head", (t3_vec3){ -0.2f, 0.24f - 0.02f * st, 0.12f - 0.24f * st });
  float c = cw_cos(yaw), sn = cw_sin(yaw);
  t3_vec3 t = { e->x + c * l.x + sn * l.z, e->y + l.y, e->z - sn * l.x + c * l.z };
  cw_node_set(r->ik[1], "target_position", cw_val_vec3(t.x, t.y, t.z));
  cw_node_set(r->ik[1], "pole_position", cw_val_vec3(e->x - c * 0.9f, e->y + 1.6f, e->z + sn * 0.9f));
  cw_node_set(r->ik[1], "influence", cw_val_float(env));
  if (p > 0.3f && p < 1.15f) {
    t3_vec3 w = rig_bone_point(&r->sr, "HandR", (t3_vec3){ 0, 0, 0 }), wt = { e->x + c * w.x + sn * w.z, e->y + w.y, e->z - sn * w.x + c * w.z };
    float dx = wt.x - t.x, dy = wt.y - t.y, dz = wt.z - t.z; int mm = (int)(sqrtf(dx * dx + dy * dy + dz * dz) * 1000.0f + 0.5f);
    g_comb[0]++; if (mm > g_comb[1]) g_comb[1] = mm;
  }
}
static void grab_hands(ent *e, float ryaw) {
  if (e->def == D_ARTHUR && e->root.id) { comb_hand(e, ryaw); return; }
  if (!e->player || !e->root.id) return;
  rig *r = &RIGS[rig_of[ent_index(e)]];
  if (!r->ik[0].id) return;
  ent *v = e->victim >= MAXP ? &W.e[e->victim - MAXP] : NULL;
  bool hold = v && v->used && v->root.id && (e->st == S_GRAB || e->st == S_THROW) && v->st == S_GRABBED && !g_legacy_grab;
  for (int k = 0; k < 2; k++) cw_node_set(r->ik[k], "active", cw_val_bool(hold));
  if (!hold) return;
  /* the holder's left hand takes the shoulder across from it: the held body faces him, so its right */
  float yaw = ent_yaw(e), side = cw_cos(yaw), sidez = -cw_sin(yaw), fx = (float)e->face;
  for (int k = 0; k < 2; k++) {
    float sg = k ? -1.0f : 1.0f; /* the holder's left is +x in his frame */
    t3_vec3 t;
    if (e->st == S_GRAB) {
      t = ent_bone_world(v, k ? "UpperArmL" : "UpperArmR");
      t.y += 0.09f, t.x -= fx * 0.1f; /* the wrist above and this side of the shoulder: the hand lies over it */
    } else {
      t = (t3_vec3){ e->x + fx * 0.05f + side * sg * 0.14f, e->y + 2.0f * BODY_SCALE[e->def] * 0.95f, e->z + sidez * sg * 0.14f };
    }
    cw_node_set(r->ik[k], "target_position", cw_val_vec3(t.x, t.y, t.z));
    cw_node_set(r->ik[k], "pole_position", cw_val_vec3(e->x + side * sg * 0.9f - fx * 0.2f, e->y + 0.7f, e->z + sidez * sg * 0.9f));
    if (e->st == S_GRAB && e->t >= 8) {
      t3_vec3 w = ent_bone_world(e, k ? "HandR" : "HandL");
      float dx = w.x - t.x, dy = w.y - t.y, dz = w.z - t.z;
      int mm = (int)(sqrtf(dx * dx + dy * dy + dz * dz) * 1000.0f + 0.5f);
      if (mm > g_grabm[1]) g_grabm[1] = mm;
      t3_vec3 c = ent_bone_world(v, "Chest");
      float cx = w.x - c.x, cz = w.z - c.z;
      mm = (int)(sqrtf(cx * cx + cz * cz) * 1000.0f + 0.5f);
      if (mm < g_grabm[2]) g_grabm[2] = mm;
      g_grabm[0]++;
    }
  }
}
/* the guard's shield. Carried: hung on the left forearm (LowerArmL, its face out of the back of the forearm: the shield's
   frame is the bone's turned half round x, see make-world.mjs shield()), so it follows the arm through every clip (it was a
   box fixed in front of the body). Floored, thrown or grabbed: it drops, falling flat onto the street in front of where
   he stood in 14 frames, face up, along the street, clear of the crates and barrels and on the road (it went into the
   street before); back on his feet he takes it up again over 10 frames. Presentation only. */
static t3_quat qslerp(t3_quat a, t3_quat b, float t) {
  float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
  if (d < 0) b = (t3_quat){ -b.x, -b.y, -b.z, -b.w }, d = -d;
  t3_quat q = { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t }; /* nlerp: short arcs */
  float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  return (t3_quat){ q.x / l, q.y / l, q.z / l, q.w / l };
}
static bool shield_carried(const ent *e) {
  return e->st == S_IDLE || e->st == S_WALK || e->st == S_ATK || e->st == S_TAUNT || e->st == S_HIT || e->st == S_JUMP || e->st == S_PICK;
}
/* debug game.shield: shields dropped, taken up again, the lowest and the highest any lying shield's lowest point sat over
   the street on any frame (mm: 0 to 15 is lying on it; under 0 is in it, more is floating), frames a lying shield was
   on a crate, barrel or bin. legacyshield 1 (the gate's control) drops it where the old box went: 0.5 m up, tilted */
int g_shield[5] = { 0, 0, 1000000, -1000000, 0 };
bool g_legacy_shield;
static const float SHIELD_PTS[][3] = { /* its lowest points (models/shield.glb): the rubber edge's corners, the handle, the brace */
  { -0.373f, -0.294f, 0.054f }, { 0.173f, -0.294f, 0.054f }, { -0.373f, 0.634f, 0.054f }, { 0.173f, 0.634f, 0.054f },
  { 0, 0.105f, 0.004f }, { 0, 0.275f, 0.004f }, { -0.05f, -0.05f, 0.04f }, { 0.05f, 0.01f, 0.04f }, { -0.1f, 0.17f, 0.124f } };
static void shield_sync(ent *e, rig *r) {
  /* where it would be on the arm, in world space */
  t3_vec3 fp, hp; t3_quat fq;
  bool arm = rig_bone_xf(&r->sr, "LowerArmL", &fp, &fq);
  hp = rig_bone_point(&r->sr, "HandL", (t3_vec3){ 0, 0, 0 });
  float yaw = ent_yaw(e), cy = cw_cos(yaw * 0.5f), sy = cw_sin(yaw * 0.5f);
  t3_quat qy = { 0, sy, 0, cy };
  t3_vec3 mid = { (fp.x + hp.x) * 0.5f, (fp.y + hp.y) * 0.5f, (fp.z + hp.z) * 0.5f };
  t3_vec3 wm = qrot4(qy, mid);
  t3_vec3 apos = { e->x + wm.x, e->y + wm.y, e->z + wm.z };
  t3_quat aq = qmul4(qy, qmul4(fq, (t3_quat){ 1, 0, 0, 0 }));
  bool carried = arm && shield_carried(e);
  if (!carried && !r->sloose) { /* it drops: flat on the road in front of him, along the street */
    r->sloose = true, r->sblend = 0, r->sp0 = apos, r->sq0 = aq;
    float x = e->x + (float)e->face * 0.45f, z = cw_clampf(e->z + 0.1f, -LANE_Z + 0.3f, LANE_Z - 0.1f);
    for (int k = 0; k < 3; k++) /* clear of the crates, barrels and bins */
      for (int i = 0; i < MAXPROP; i++) {
        const prop_t *p = &W.prop[i];
        if (p->used && fabsf(p->x - x) < 0.95f && fabsf(p->z - z) < 0.75f) x += (x >= p->x ? 1.0f : -1.0f) * (0.95f - fabsf(p->x - x) + 0.05f);
      }
    /* face up: the shield's +z onto world +y (half a turn down about x), its length along the street (a quarter turn about y) */
    const float h = 0.70710678f;
    r->sq1 = qmul4((t3_quat){ 0, h, 0, h }, (t3_quat){ -h, 0, 0, h });
    r->sp1 = (t3_vec3){ x, -0.002f, z }; /* the handle behind it is its lowest point, 4 mm over the frame origin: it rests 2 mm up */
    if (g_legacy_shield) { /* the old placement: 0.5 m up in front of the root, tilted 1.2 rad */
      t3_vec3 o = qrot4(qy, (t3_vec3){ 0.1f, 0.5f, 0.25f });
      r->sp1 = (t3_vec3){ e->x + o.x, e->y + o.y, e->z + o.z };
      r->sq1 = qmul4(qy, (t3_quat){ cw_sin(0.6f), 0, 0, cw_cos(0.6f) });
    }
    g_shield[0]++;
  } else if (carried && r->sloose && r->sblend >= 14) r->sloose = false, r->spick = true, r->sblend = 0, r->sp0 = r->sp1, r->sq0 = r->sq1, g_shield[1]++; /* taken up again */
  t3_vec3 pos; t3_quat q;
  if (r->sloose) {
    float u = cw_clampf((float)r->sblend / 14.0f, 0, 1);
    if (r->sblend < 14) r->sblend++;
    float uf = u * u; /* it falls */
    pos = (t3_vec3){ r->sp0.x + (r->sp1.x - r->sp0.x) * u, r->sp0.y + (r->sp1.y - r->sp0.y) * uf, r->sp0.z + (r->sp1.z - r->sp0.z) * u };
    if (pos.y < r->sp1.y) pos.y = r->sp1.y;
    q = qslerp(r->sq0, r->sq1, u);
    if (u >= 1) { /* lying: its lowest point over the street, and never on a prop */
      float low = 1e9f;
      for (size_t k = 0; k < sizeof SHIELD_PTS / sizeof *SHIELD_PTS; k++) {
        t3_vec3 w = qrot4(q, (t3_vec3){ SHIELD_PTS[k][0], SHIELD_PTS[k][1], SHIELD_PTS[k][2] });
        if (pos.y + w.y < low) low = pos.y + w.y;
      }
      int mm = (int)floorf(low * 1000.0f + 0.5f);
      if (mm < g_shield[2]) g_shield[2] = mm;
      if (mm > g_shield[3]) g_shield[3] = mm;
      for (int i = 0; i < MAXPROP; i++) if (W.prop[i].used && fabsf(W.prop[i].x - pos.x) < 0.75f && fabsf(W.prop[i].z - pos.z) < 0.6f) { g_shield[4]++; break; }
    }
  } else if (r->spick) { /* the pick-up: from the street to the arm */
    float u = (float)(++r->sblend) / 10.0f, k = u * u * (3 - 2 * u);
    if (r->sblend >= 10) r->spick = false;
    pos = (t3_vec3){ r->sp0.x + (apos.x - r->sp0.x) * k, r->sp0.y + (apos.y - r->sp0.y) * k, r->sp0.z + (apos.z - r->sp0.z) * k };
    q = qslerp(r->sq0, aq, k);
  } else pos = apos, q = aq;
  cw_node_set(r->shield, "visible", cw_val_bool(arm || r->sloose));
  cw_node3d_set_position(r->shield, pos);
  cw_node3d_set_quaternion(r->shield, q);
}
/* a held prop gripped in the right fist: it follows the hand's turn as well as its place (it stood upright beside the
   hand whatever the arm did, the 0.1.1 review). The props are modelled along +y (make-world.mjs); a fist closes round an
   axis across the palm, from the little finger to the thumb, which is the hand bone's +z, so the prop's +y goes there, and
   the grip (WGRIP, along the prop) sits in the fist: inside the knuckle line, a little toward the palm */
static t3_quat qmul4(t3_quat a, t3_quat b) {
  return (t3_quat){ a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                    a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}
static t3_vec3 qrot4(t3_quat q, t3_vec3 v) {
  t3_vec3 u = { q.x, q.y, q.z };
  t3_vec3 t = { 2 * (u.y * v.z - u.z * v.y), 2 * (u.z * v.x - u.x * v.z), 2 * (u.x * v.y - u.y * v.x) };
  return (t3_vec3){ v.x + q.w * t.x + (u.y * t.z - u.z * t.y), v.y + q.w * t.y + (u.z * t.x - u.x * t.z), v.z + q.w * t.z + (u.x * t.y - u.y * t.x) };
}
static const float WGRIP7[8] = { 0, -0.3f, -0.38f, -0.11f, 0, 0, -0.05f, -0.21f }; /* along the prop: the middle of its grip (6: the comb's spine end, 7: the guard's baton) */
int g_held[3]; bool g_legacy_hold; static int held_lo, held_hi = -1;
static void hold_in_hand(ent *e, rig *r, int kind) {
  t3_vec3 hp; t3_quat hq;
  if (!rig_bone_xf(&r->sr, "HandR", &hp, &hq)) return;
  t3_vec3 k1 = rig_bone_point(&r->sr, "FingR1", (t3_vec3){ 0, 0, 0 });
  /* the fist's centre: most of the way from the wrist to the knuckles, toward the palm (+x for the right hand) */
  t3_vec3 palm = qrot4(hq, (t3_vec3){ 0.022f, 0, 0.004f });
  t3_vec3 grip = { hp.x + (k1.x - hp.x) * 0.82f + palm.x, hp.y + (k1.y - hp.y) * 0.82f + palm.y, hp.z + (k1.z - hp.z) * 0.82f + palm.z };
  const float h = 0.70710678f;
  t3_quat q = qmul4(hq, (t3_quat){ h, 0, 0, h }); /* the prop's +y onto the hand's +z */
  if (kind == 6) q = (t3_quat){ 0, 0, 0.3146f, 0.9492f }; /* the comb stands up and out of the fist past the side of his hair, where the street camera sees it (along the hand's own axis it sat inside the hair) */
  t3_vec3 off = qrot4(q, (t3_vec3){ 0, WGRIP7[kind] * (kind == 6 ? 1.6f : 1.0f), 0 });
  t3_vec3 pos = { grip.x - off.x, grip.y - off.y, grip.z - off.z };
  if (g_legacy_hold) { /* the old placement (the hold gate's control): upright, at the hand */
    t3_vec3 w = cw_skeleton_bone_world(e->model, "HandR");
    pos = (t3_vec3){ w.x, w.y - WGRIP7[kind], w.z }, q = (t3_quat){ 0, 0, 0, 1 };
  }
  cw_node3d_set_position(r->wmodel, pos);
  cw_node3d_set_quaternion(r->wmodel, q);
  { float sc = kind == 6 ? 1.6f : 1.0f; cw_node3d_set_scale(r->wmodel, (t3_vec3){ sc, sc, sc }); } /* the comb is drawn big enough to read from the street camera */
  if (e->player) { /* game.held: frames, how far from the fist its grip was at worst (mm), how many degrees the prop's axis turned over the run */
    t3_vec3 gw = qrot4(q, (t3_vec3){ 0, WGRIP7[kind], 0 }), ax = qrot4(q, (t3_vec3){ 0, 1, 0 });
    float dx = pos.x + gw.x - grip.x, dy = pos.y + gw.y - grip.y, dz = pos.z + gw.z - grip.z;
    int mm = (int)(sqrtf(dx * dx + dy * dy + dz * dz) * 1000.0f + 0.5f);
    if (mm > g_held[1]) g_held[1] = mm;
    int deg = (int)(cw_acos(cw_clampf(ax.y, -1, 1)) * 57.2958f + 0.5f);
    if (g_held[0]++ == 0 || deg < held_lo) held_lo = deg;
    if (deg > held_hi) held_hi = deg;
    g_held[2] = held_hi - held_lo;
  }
}
static const float HAND_OFF[3] = { 0, -0.05f, 0.05f };
void char_sync(ent *e) {
  if (!e->root.id) return;
  cw_node3d_set_position(e->root, (t3_vec3){ e->x, e->y, e->z });
  {
    /* the soft shadow on the ground: stays under the actor, smaller and fainter in the air */
    rig *r = &RIGS[rig_of[ent_index(e)]];
    float bs = 0.75f * BODY_SCALE[e->def] * (1.0f - 0.25f * cw_clampf(e->y, 0, 2) / 2.0f);
    cw_node3d_set_position(r->blob, (t3_vec3){ e->x, 0.03f, e->z });
    cw_node3d_set_scale(r->blob, (t3_vec3){ bs, 1, bs });
  }
  {
    rig *r = &RIGS[rig_of[ent_index(e)]];
    if (r->shield.id) shield_sync(e, r);
  }
  float yaw = ent_yaw(e);
  if (e->clipname && (!strcmp(e->clipname, "thumbsup") || !strcmp(e->clipname, "comb")) && e->anim.id && !g_freeze) { /* the thumbs up and the comb are for the audience: he turns three quarters to the camera, then back (side on, facing left, the comb was behind his head) */
    float p = (float)cw_anim_player_position(e->anim), k = cw_clampf(p / 0.25f, 0, 1) * (1 - cw_clampf((p - 1.2f) / 0.3f, 0, 1));
    k = k * k * (3 - 2 * k);
    yaw += ((float)e->face * 0.5f - yaw) * k;
  }
  t3_vec3 roff;
  if (rag_render(e, &yaw, &roff)) cw_node3d_set_position(e->root, (t3_vec3){ e->x + roff.x, e->y, e->z + roff.z }); /* a ragdoll or a get-up turned to lie the way the body fell */
  if (e->st == S_GRABBED && e->pitch > 0) { /* hoisted: tipped forward on his feet, flat over the holder's head */
    t3_quat qy = { 0, cw_sin(yaw * 0.5f), 0, cw_cos(yaw * 0.5f) }, qp = { cw_sin(e->pitch * 0.5f), 0, 0, cw_cos(e->pitch * 0.5f) };
    cw_node3d_set_quaternion(e->root, qmul4(qy, qp));
  } else cw_node3d_set_rotation(e->root, (t3_vec3){ 0, yaw, 0 });
  grab_hands(e, yaw);
  rig *wr = &RIGS[rig_of[ent_index(e)]];
  if (e->def == D_ARTHUR && !e->weapon) { /* his comb comes out for the taunt (a pocket comb in his right hand) */
    bool comb = e->clipname && !strcmp(e->clipname, "comb");
    if (comb && wr->wkind != 6) { t3_object *o = item_model(6); if (o) { cw_node3d_set_object(wr->wmodel, o); t3_release(o); wr->wkind = 6; } }
    cw_node_set(wr->wmodel, "visible", cw_val_bool(comb));
    if (comb) hold_in_hand(e, wr, 6);
  }
  if (e->weapon) hold_in_hand(e, wr, e->weapon & 3);
  else if (e->def == D_GUARD) { /* the riot guard's baton in his right fist (presentation: his blows are the shield shove) */
    if (wr->wkind != 7) { t3_object *o = item_model(7); if (o) { cw_node3d_set_object(wr->wmodel, o); t3_release(o); wr->wkind = 7; } }
    cw_node_set(wr->wmodel, "visible", cw_val_bool(true));
    hold_in_hand(e, wr, 7);
  }
  (void)HAND_OFF;
}

/* render side: the bottles in flight, and a danger ring on the street under every heavy attack while it winds up and
   swings (the telegraph players read: the ring grows brighter until the blow) */
static cw_node PROJN[8];
void sim_sync_extras(void) {
  { extern int g_rings; g_rings = 0; }
  for (int i = 0; i < MAXP; i++) { /* the fighters' rings: fainter while they fall or fly, none for the dead */
    const ent *p = &W.p[i];
    float k = i < W.players && p->used && p->st != S_DEAD ? 1.0f - cw_clampf(p->y, 0, 1.5f) / 1.5f * 0.7f : 0;
    fx_player_ring(i, p->x, p->z, k);
  }
  for (int i = 0; i < 8; i++) {
    const struct proj *p = &W.proj[i];
    if (!PROJN[i].id || !cw_node_valid(PROJN[i])) {
      if (!p->used) continue;
      char nm[16];
      snprintf(nm, sizeof nm, "Bottle%d", i);
      PROJN[i] = cw_node_new("Node3D", nm);
      cw_node_add_child(world_node(), PROJN[i]);
      t3_object *o = item_model(5); /* a green glass bottle */
      if (o) { cw_node3d_set_object(PROJN[i], o); t3_release(o); }
    }
    cw_node_set(PROJN[i], "visible", cw_val_bool(p->used));
    if (!p->used) continue;
    cw_node3d_set_position(PROJN[i], (t3_vec3){ p->x, p->y, p->z });
    cw_node3d_set_rotation(PROJN[i], (t3_vec3){ 0, 0, (float)p->t * 0.45f * (p->vx > 0 ? -1.0f : 1.0f) });
  }
  int k = 0;
  for (int i = 0; i < MAXE && k < 8; i++) {
    const ent *e = &W.e[i];
    if (!e->used || e->st != S_ATK || !heavy_atk(e->atk)) continue;
    const atk_data *ad = &ATK[e->atk];
    if (e->t > ad->a1) continue;
    int tele = e->atk == A_RUSH ? 40 : 26;
    float glow = e->t < 0 ? 0.25f + 0.75f * (1.0f - (float)-e->t / (float)tele) : 1.0f;
    float f = (float)e->face;
    if (e->atk == A_SLAM || e->atk == A_BSPIN) fx_warn(k++, e->x, e->z, ad->reach, ad->reach, glow);
    else if (e->atk == A_SWING || e->atk == A_FLICK) fx_warn(k++, e->x + f * ad->reach * 0.5f, e->z, ad->reach * 0.6f, 0.7f, glow);
    else { float len = e->t < 0 ? 3.2f : 1.6f; fx_warn(k++, e->x + f * len, e->z, len, 0.75f, glow); }
  }
  for (; k < 8; k++) fx_warn(k, 0, 0, 0, 0, 0);
}

/* ── rig access for the ragdolls (rag.c: presentation only) ───────────── */
int rig_id_of(const ent *e) { return e->root.id ? rig_of[ent_index(e)] : -1; }
cw_node rig_model(int rid) { return RIGS[rid].model; }
cw_node rig_root(int rid) { return RIGS[rid].root; }
void rig_release(int rid) { /* a corpse rag.c kept on the street is done: hide it and give the rig back */
  rig *r = &RIGS[rid];
  cw_node_set(r->wnode, "visible", cw_val_bool(false));
  cw_node_set(r->wmodel, "visible", cw_val_bool(false));
  cw_node_set(r->root, "visible", cw_val_bool(false));
  cw_node_set(r->blob, "visible", cw_val_bool(false));
  if (r->shield.id) cw_node_set(r->shield, "visible", cw_val_bool(false));
  cw_node3d_set_position(r->root, (t3_vec3){ 0, -50, 0 });
  r->busy = false;
}
float ent_render_yaw_sim(const ent *e) { return ent_yaw(e); }
float char_scale(int def) { return BODY_SCALE[def]; }
/* debug "bone <ent> <name>": a bone's place and its axes in the model's space (ent 0-3 fighters, 4+ enemies) */
bool debug_bone(int ei, const char *name, float out[12]) {
  ent *e = ei < MAXP ? &W.p[ei] : &W.e[(ei - MAXP) % MAXE];
  if (!e->used || !e->root.id) return false;
  t3_vec3 p; t3_quat q;
  if (!rig_bone_xf(&RIGS[rig_of[ent_index(e)]].sr, name, &p, &q)) return false;
  t3_vec3 ax = qrot4(q, (t3_vec3){ 1, 0, 0 }), ay = qrot4(q, (t3_vec3){ 0, 1, 0 }), az = qrot4(q, (t3_vec3){ 0, 0, 1 });
  float v[12] = { p.x, p.y, p.z, ax.x, ax.y, ax.z, ay.x, ay.y, ay.z, az.x, az.y, az.z };
  memcpy(out, v, sizeof v);
  return true;
}
void char_play_force(ent *e, const char *clip, float blend) { char_restart(e, clip, blend); }
/* debug: an enemy's face weight for target t (the boss's expression in the gates and screenshots) */
float face_weight_of_enemy(int i, int t) { return i >= 0 && i < MAXE && W.e[i].used && W.e[i].root.id ? face_weight(&RIGS[rig_of[MAXP + i]].sr, t) : 0; }
