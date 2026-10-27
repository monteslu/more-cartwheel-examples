/* brawler simulation: fighters, combos, grabs, weapons, enemy crowd AI with attack tokens,
 * bosses. Runs in fixed_update from the input snapshot and cw_rng only. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/anim.h"
#include "cartwheel/ik.h"
#include "cartwheel/nav.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/vfs.h"
#include "game.h"

world_t W;
static cw_crowd crowd;
static cw_navmesh navm;

const char_def DEFS[D_COUNT] = {
  [D_ROOK] = { "Rook", "rook", 3.0f, 150, 1.25f, false, false, 0 },
  [D_MIKA] = { "Mika", "mika", 3.9f, 105, 1.0f, false, false, 0 },
  [D_JET] = { "Jet", "jet", 3.4f, 120, 1.1f, false, false, 0 },
  [D_THUG] = { "Thug", "thug", 2.0f, 42, 1.0f, false, false, 100 },
  [D_PUNK] = { "Punk", "punk", 2.4f, 36, 1.0f, false, false, 120 },
  [D_RUNNER] = { "Runner", "runner", 2.6f, 30, 1.0f, false, false, 150 },
  [D_KNIFER] = { "Knifer", "knifer", 2.3f, 48, 1.0f, false, false, 200 },
  [D_BRUISER] = { "Bruiser", "bruiser", 1.9f, 240, 1.2f, false, true, 600 },
  [D_FOREMAN] = { "The Foreman", "foreman", 1.9f, 700, 1.1f, true, false, 2000 },
  [D_MATRON] = { "The Matron", "matron", 2.5f, 800, 1.0f, true, false, 3000 },
  [D_RINGMASTER] = { "Ringmaster", "ringmaster", 2.6f, 900, 1.1f, true, false, 4000 },
  [D_COLOSSUS] = { "Colossus", "colossus", 2.2f, 1500, 1.3f, true, false, 8000 },
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
};

/* ── character rigs: a pool per model, so a spawn never parses a glb twice ── */
typedef struct rig { int def; bool busy; cw_node root, model, anim, wnode, blob; t3_gltf *gltf, *blobg; } rig;
static const float BODY_SCALE[D_COUNT] = { 1.08f, 0.98f, 1.0f, 1.0f, 0.96f, 0.94f, 1.0f, 1.28f, 1.4f, 1.22f, 1.15f, 1.7f };
static rig RIGS[96];
static int rig_n;
static cw_node world_node(void) { return cw_node_get(cw_node_get(cw_tree_root(cw_scene_app_tree()), "Level"), "World"); }
static int rig_of[MAXP + MAXE];

static void set_shadows(t3_object *o) {
  if (o->type == T3_SKINNED_MESH || o->type == T3_MESH) o->cast_shadow = true, o->receive_shadow = true;
  for (int i = 0; i < o->child_count; i++) set_shadows(o->children[i]);
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
  size_t len;
  void *glb = cw_asset_load(path, &len);
  if (!glb) { cw_error("brawler: %s missing", path); return NULL; }
  r->gltf = t3_gltf_parse(glb, len, NULL, NULL);
  free(glb);
  char nm[32];
  snprintf(nm, sizeof nm, "Rig%d", rig_n);
  r->root = cw_node_new("Node3D", nm);
  r->model = cw_node_new("Node3D", "Model");
  r->anim = cw_node_new("AnimationPlayer", "Anim");
  r->wnode = cw_node_new("MeshInstance3D", "Weapon");
  r->blob = cw_node_new("Node3D", "Blob");
  cw_node_add_child(world_node(), r->blob);
  { size_t bl; void *bg = cw_asset_load("models/blob.glb", &bl); if (bg) { r->blobg = t3_gltf_parse(bg, bl, NULL, NULL); free(bg); if (r->blobg) cw_node3d_set_object(r->blob, r->blobg->scene); } }
  cw_node_add_child(world_node(), r->root);
  cw_node_add_child(r->root, r->model);
  cw_node_add_child(r->root, r->anim);
  cw_node_add_child(r->root, r->wnode);
  cw_node3d_set_object(r->model, r->gltf->scene);
  set_shadows(r->gltf->scene);
  for (int i = 0; i < r->gltf->animation_count; i++) {
    cw_anim a = cw_anim_from_t3_clip(r->gltf->animations[i], "Model");
    cw_anim_set_stored(a, false);
    cw_anim_player_add(r->anim, r->gltf->animations[i]->name, a);
    cw_anim_release(a);
  }
  cw_node_set(r->wnode, "primitive", cw_val_enum("BOX"));
  cw_node_set(r->wnode, "size", cw_val_vec3(0.07f, 0.8f, 0.07f));
  cw_node_set(r->wnode, "visible", cw_val_bool(false));
  cw_node_set(r->wnode, "cast_shadow", cw_val_bool(true));
  r->busy = true;
  return r;
}

static int ent_index(const ent *e) { return e->player ? (int)(e - W.p) : MAXP + (int)(e - W.e); }

void char_attach(ent *e) {
  rig *r = rig_get(e->def);
  if (!r) return;
  rig_of[ent_index(e)] = (int)(r - RIGS);
  e->root = r->root, e->model = r->model, e->anim = r->anim, e->wnode = r->wnode;
  cw_node_set(r->blob, "visible", cw_val_bool(true));
  e->clipname = NULL;
  cw_node_set(e->root, "visible", cw_val_bool(true));
  cw_node_set(e->anim, "speed_scale", cw_val_float(1));
  char_play(e, "idle", 0);
}
void char_free(ent *e) {
  if (!e->root.id) return;
  cw_node_set(e->wnode, "visible", cw_val_bool(false));
  cw_node_set(e->root, "visible", cw_val_bool(false));
  cw_node_set(RIGS[rig_of[ent_index(e)]].blob, "visible", cw_val_bool(false));
  cw_node3d_set_position(e->root, (t3_vec3){ 0, -50, 0 });
  RIGS[rig_of[ent_index(e)]].busy = false;
  e->root = (cw_node){ 0 };
}
void char_play(ent *e, const char *clip, float blend) {
  if (!e->anim.id) return;
  if (e->clipname == clip) return;
  e->clipname = clip;
  cw_anim_player_play(e->anim, clip, blend);
  cw_anim_player_seek(e->anim, 0, false);
}
static void char_restart(ent *e, const char *clip, float blend) {
  e->clipname = NULL;
  char_play(e, clip, blend);
}

/* weapon colours and placement are set when the weapon is held */
static const float WLEN[4] = { 0, 0.85f, 1.0f, 0.34f };
static void weapon_show(ent *e) {
  if (!e->wnode.id) return;
  bool vis = e->weapon > 0;
  cw_node_set(e->wnode, "visible", cw_val_bool(vis));
  if (!vis) return;
  static const float col[4][3] = { { 0, 0, 0 }, { 0.45f, 0.28f, 0.12f }, { 0.35f, 0.38f, 0.42f }, { 0.75f, 0.78f, 0.85f } };
  cw_node_set(e->wnode, "size", cw_val_vec3(e->weapon == 3 ? 0.04f : 0.07f, WLEN[e->weapon], e->weapon == 3 ? 0.02f : 0.07f));
  cw_node_set(e->wnode, "color", cw_val_color(col[e->weapon][0], col[e->weapon][1], col[e->weapon][2], 1));
  cw_node_set(e->wnode, "metalness", cw_val_float(e->weapon >= 2 ? 0.8f : 0.0f));
  cw_node_set(e->wnode, "roughness", cw_val_float(0.45f));
}

/* ── small helpers ──────────────────────────────────────────────────── */
static float rf(float a, float b) { return cw_rng_range(&W.rng, a, b); }
static int ri(int a, int b) { return cw_rng_int(&W.rng, a, b); }
static float absf(float x) { return x < 0 ? -x : x; }
static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static bool alive(const ent *e) { return e->used && e->st != S_DEAD && e->hp > 0; }
static bool free_state(const ent *e) { return e->st == S_IDLE || e->st == S_WALK; }

float level_cam_halfw(void) { return 7.2f; }

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
  p.radius = 0.38f;
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
  e->hp = e->maxhp = DEFS[def].hp * (player ? 1.0f : (W.players > 1 ? 1.0f + 0.35f * (float)(W.players - 1) : 1.0f));
  e->st = S_IDLE, e->target = -1, e->victim = -1, e->holder = -1;
  e->cd = 40 + ri(0, 60);
  e->slot = ri(0, 9);
  e->agent = agent_add(x, z, DEFS[def].speed, player);
  char_attach(e);
}

int ent_spawn_enemy(int def, float x, float z) {
  for (int i = 0; i < MAXE; i++)
    if (!W.e[i].used) {
      ent_init(&W.e[i], def, false, x, z);
      W.e[i].idx = i;
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

void ent_hurt(ent *v, ent *a, float dmg, int knock, float fx) {
  (void)fx;
  if (!alive(v) || v->invuln > 0) return;
  bool boss = DEFS[v->def].boss || DEFS[v->def].mini;
  if (v->player && v->st == S_JUMP && knock < 2) knock = 1;
  v->hp -= dmg;
  v->hurtflash = 1;
  int dir = a ? (a->x <= v->x ? 1 : -1) : -v->face;
  W.hitstop = knock >= 2 ? 7 : 4;
  W.shake = knock >= 2 ? 10 : 4;
  W.shake_amp = knock >= 2 ? 0.12f : 0.05f;
  fx_hit(v->x, v->y + 1.1f, v->z, knock, dir);
  sfx(knock >= 2 ? SFX_HIT3 : ((int)v->hp & 1) ? SFX_HIT1 : SFX_HIT2);
  if (a && a->player) {
    a->score += (int)dmg * 10;
    W.combo_n++, W.combo_t = 100;
    W.total_score += (int)dmg * 10;
  }
  release_pair(v);
  if (v->victim >= 0) { /* a grabber that is hit lets go */
    ent *u = v->victim < MAXP ? &W.p[v->victim] : &W.e[v->victim - MAXP];
    u->holder = -1;
    if (u->st == S_GRABBED) u->st = S_IDLE;
    v->victim = -1;
  }
  if (v->token) { v->token = 0; W.tokens_out--; }
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
        if (a && a->player) a->score += DEFS[v->def].score;
        W.total_score += DEFS[v->def].score;
        drop_weapon(v);
        if (DEFS[v->def].boss) { W.shake = 40; W.shake_amp = 0.2f; W.slow = 90; }
        /* a coin or two */
        for (int k = 0; k < (DEFS[v->def].boss ? 6 : 1); k++)
          for (int i = 0; i < MAXITEM; i++)
            if (!W.item[i].used) { W.item[i] = (item_t){ .used = true, .kind = 4, .x = v->x + rf(-0.6f, 0.6f), .z = v->z + rf(-0.4f, 0.4f), .y = 0.5f }; break; }
      }
      sfx(SFX_KO);
    }
  } else if (!boss || v->st != S_ATK) {
    /* bosses and the mini boss do not flinch out of an attack */
    if (!(boss && v->st == S_ATK)) {
      v->st = S_HIT, v->t = 0;
      v->stun = knock >= 1 ? 22 : 15;
      v->x += (float)dir * 0.18f;
      char_restart(v, knock >= 1 ? "hit_hard" : "hit", 0.02f);
    }
  }
  if (boss && v->st != S_FALL && v->st != S_HIT) v->hurtflash = 1;
}

/* ── attacks ────────────────────────────────────────────────────────── */
static void start_attack(ent *e, akind k) {
  e->st = S_ATK, e->atk = k, e->t = 0, e->hitmask = 0, e->chain_ok = 0;
  const atk_data *a = &ATK[k];
  if (!e->player) { /* a readable telegraph: guard pose and a flash before the swing (12+ frames, 24+ for heavies) */
    e->t = (k == A_SLAM || k == A_SWING || k == A_BSPIN) ? -26 : -14;
    char_restart(e, "block", 0.06f);
    e->hurtflash = 1.4f;
  } else char_restart(e, a->clip, 0.04f);
  if (k == A_SLAM || k == A_SPIN || k == A_BSPIN) sfx(SFX_WHIFF);
}

static bool in_box(const ent *a, const ent *v, float reach, float width) {
  float dx = (v->x - a->x) * (float)a->face;
  return dx > -0.25f && dx < reach && absf(v->z - a->z) < (width > 1.2f ? width : 0.62f) && absf(v->y - a->y) < 1.3f;
}
static bool in_round(const ent *a, const ent *v, float r) {
  float dx = v->x - a->x, dz = v->z - a->z;
  return dx * dx + dz * dz < r * r && absf(v->y - a->y) < 1.4f;
}

static void do_hits(ent *a) {
  const atk_data *ad = &ATK[a->atk];
  if (a->t < ad->a0 || a->t > ad->a1) return;
  float mul = DEFS[a->def].dmg * (a->player ? 1.0f : 0.8f);
  float dmg = ad->dmg * mul;
#ifdef BRAWLER_AUTOPILOT
  if (a->player) dmg *= 3.0f; /* the bots are poor fighters: the soak needs to reach the bosses */
#endif
  if (a->weapon && (a->atk == A_SWING || a->atk == A_STAB)) dmg *= a->weapon == 2 ? 1.25f : 1.0f;
  bool round = a->atk == A_SPIN || a->atk == A_SLAM || a->atk == A_BSPIN;
  bool hit_any = false;
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
  if (a->atk == A_SLAM && a->t == ad->a0) { fx_dust(a->x + (float)a->face * 1.6f, a->z, 3.0f); W.shake = 14; W.shake_amp = 0.14f; sfx(SFX_SLAM); }
}

/* ── shared states ──────────────────────────────────────────────────── */
static void arena_clamp(ent *e) {
  float hw = level_cam_halfw();
  float lo = W.cam_x - hw + 0.5f, hi = W.cam_x + hw - 0.5f;
  if (e->player) e->x = clampf(e->x, lo, hi);
  else e->x = clampf(e->x, lo - 3.5f, hi + 3.5f);
  e->z = clampf(e->z, -LANE_Z, LANE_Z);
}

/* returns true when the shared state handled this tick */
static bool common_state(ent *e) {
  switch (e->st) {
  case S_HIT:
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
      W.shake = 6, W.shake_amp = 0.06f;
      char_play(e, e->hp <= 0 ? "dead" : "down", 0.02f);
      if (e->hp <= 0) e->st = S_DEAD;
    }
    return true;
  case S_THROWN: {
    e->t++;
    e->x += e->vx / TPS, e->y += e->vy / TPS;
    e->vy -= 18.0f / TPS;
    /* a thrown body bowls over whoever it hits */
    for (int i = 0; i < (e->player ? MAXE : MAXP); i++) {
      ent *v = e->player ? &W.e[i] : &W.p[i];
      if (!alive(v) || v == e || v->st == S_DOWN || v->st == S_FALL || v->st == S_THROWN || (e->hitmask & (1u << (i & 31)))) continue;
      if (in_round(e, v, 0.9f)) { e->hitmask |= 1u << (i & 31); ent_hurt(v, e, 10, 2, 0); }
    }
    if (e->y <= 0 && e->vy < 0) {
      e->y = 0;
      fx_dust(e->x, e->z, 1.6f);
      e->vx *= 0.3f;
      e->st = S_FALL, e->t = 0, e->vy = 2.0f;
      char_restart(e, "knockdown", 0.02f);
      ent *none = NULL;
      ent_hurt(e, none, 0, 0, 0); /* dust only: invuln guards re-entry */
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
    if (++e->t > 54) { e->st = S_IDLE, e->t = 0; e->invuln = 40; char_play(e, "idle", 0.1f); }
    return true;
  case S_DEAD:
    e->t++;
    return true;
  case S_GRABBED: {
    ent *h = e->holder < 0 ? NULL : (e->holder < MAXP ? &W.p[e->holder] : &W.e[e->holder - MAXP]);
    if (!h || !alive(h) || h->victim != ent_index(e)) { e->st = S_IDLE; e->holder = -1; return true; }
    e->x = h->x + (float)h->face * 0.75f, e->z = h->z, e->y = 0;
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
    char_play(e, mv ? (spd > 3.5f ? "run" : "walk") : "idle", 0.12f);
    if (in->jump) { e->st = S_JUMP, e->vy = 6.8f, e->vx = mx * spd, e->t = 0; char_restart(e, "jump", 0.05f); break; }
    if (in->grab) { /* grab a body in front, or a weapon underfoot */
      int best = -1;
      for (int i = 0; i < MAXE; i++) {
        ent *v = &W.e[i];
        if (!alive(v) || DEFS[v->def].boss || v->st == S_DOWN || v->st == S_FALL || v->st == S_THROWN || v->st == S_GRABBED || v->st == S_DEAD) continue;
        if (in_box(e, v, 1.05f, 0.7f) && (best < 0 || absf(v->z - e->z) < absf(W.e[best].z - e->z))) best = i;
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
      if (e->atk == A_PUNCH1 || e->atk == A_PUNCH2) e->combo_t = 40;
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
      e->st = S_THROW, e->t = 0;
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
      ent_hurt(v, e, 6 * DEFS[e->def].dmg, 0, 0);
      if (alive(v)) { v->st = S_GRABBED; v->holder = pi; char_restart(v, "grabbed", 0.0f); }
      e->hold = 4;
      if (e->knees >= 3) { e->st = S_THROW, e->t = 0; char_restart(e, "throw", 0.04f); sfx(SFX_THROW); e->face = -e->face; }
    }
    break;
  }
  case S_THROW: {
    ent *v = e->victim < 0 ? NULL : &W.e[e->victim - MAXP];
    e->t++;
    if (v && e->t == 15 && v->st == S_GRABBED) {
      v->st = S_THROWN, v->holder = -1, v->t = 0, v->hitmask = 0;
      v->vx = -(float)e->face * 9.0f, v->vy = 5.0f, v->y = 0.2f;
      char_restart(v, "thrown", 0.02f);
      ent_hurt(v, e, 16 * DEFS[e->def].dmg, 0, 0);
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
    if (d2 < 0.2f) { float d = sqrtf(d2) + 0.001f; e->x += dx / d * 0.02f; e->z += dz / d * 0.02f; if (d2 < 0.0001f) e->z += (pi > k ? 0.03f : -0.03f); }
  }
  arena_clamp(e);
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
  case D_FOREMAN:
    if (e->phase == 0 || ri(0, 2)) start_attack(e, ri(0, 1) ? A_SWING : A_PUNCH2);
    else start_attack(e, A_SLAM);
    break;
  case D_MATRON: start_attack(e, ri(0, 2) == 0 ? A_BSPIN : (ri(0, 1) ? A_KICK : A_STAB)); break;
  case D_RINGMASTER: start_attack(e, ri(0, 2) == 0 ? A_SLAM : A_SWING); break;
  case D_COLOSSUS: start_attack(e, ri(0, 3) == 0 ? A_BSPIN : (ri(0, 1) ? A_SLAM : A_SWING)); break;
  default: start_attack(e, A_PUNCH1);
  }
  (void)d;
}

static void enemy_update(int ei) {
  ent *e = &W.e[ei];
  if (!e->used) return;
  apply_invuln(e);
  if (e->hold > 0) { e->hold--; return; }
  if (e->cd > 0) e->cd--;
  if (common_state(e)) {
    if (e->st == S_DEAD && e->t > 70) {
      e->fade = 1;
    }
    arena_clamp(e);
    return;
  }
  const char_def *d = &DEFS[e->def];
  bool boss = d->boss;
  switch (e->st) {
  case S_ATK: {
    const atk_data *ad = &ATK[e->atk];
    e->t++;
    if (e->t == 0) char_restart(e, ad->clip, 0.05f);
    if (e->t < 0) { e->hurtflash = 0.6f; break; }
    if (e->atk == A_CHARGE) {
      e->x += e->vx / TPS;
      if (e->t > ad->a1) e->vx *= 0.8f;
    } else if (e->t < ad->a1 && ad->lunge > 0) e->x += (float)e->face * ad->lunge * 4.0f / TPS;
    do_hits(e);
    if (e->t >= ad->total) {
      e->st = S_IDLE, e->t = 0, e->atk = A_NONE;
      if (e->token) { e->token = 0; W.tokens_out--; }
      e->cd = boss ? 40 + ri(0, 40) : 70 + ri(0, 90);
      if (e->def == D_THUG && ri(0, 3) == 0) { e->cd = 20; } /* a quick second swing now and then */
    }
    break;
  }
  default: {
    int ti = nearest_player(e);
    if (ti < 0) { e->st = S_IDLE; char_play(e, "idle", 0.2f); if (e->agent && crowd.id) cw_crowd_set_velocity(crowd, AG(e->agent), (t3_vec3){ 0, 0, 0 }); break; }
    ent *p = &W.p[ti];
    float side = (e->x < p->x) ? -1.0f : 1.0f; /* the side it is already on */
    float dx = p->x - e->x, dz = p->z - e->z;
    float reach = boss ? 1.3f : 0.95f;
    /* token: one request per think */
    int held = 0; /* attackers already on this target: two at most per fighter */
    for (int k = 0; k < MAXE; k++) if (W.e[k].used && W.e[k].token && W.e[k].target == ti && !DEFS[W.e[k].def].boss) held++;
    bool want = !e->token && e->cd == 0 && (boss || held < 2);
    float dist = sqrtf(dx * dx + dz * dz);
    if (want && dist < 5.0f) { e->token = 1; e->target = ti; W.tokens_out++; }
    float tx, tz;
    if (e->token) {
      tx = p->x + side * reach * 0.9f;
      tz = p->z;
    } else { /* wait in a ring around the player */
      float rr = 2.4f + (float)(e->slot % 3) * 0.55f;
      float sd = (e->slot & 1) ? 1.0f : -1.0f;
      tx = p->x + sd * rr;
      tz = p->z + ((float)(e->slot % 5) - 2.0f) * 0.55f;
    }
    tz = clampf(tz, -LANE_Z + 0.2f, LANE_Z - 0.2f);
    if (crowd.id && e->agent) {
      e->thinkt++;
      if (e->thinkt % 6 == 0) cw_crowd_set_target(crowd, AG(e->agent), (t3_vec3){ tx, 0, tz });
    }
    float to_x = tx - e->x, to_z = tz - e->z;
    bool arrived = to_x * to_x + to_z * to_z < 0.12f;
    e->face = dx >= 0 ? 1 : -1;
    e->st = arrived ? S_IDLE : S_WALK;
    char_play(e, arrived ? "idle" : (d->speed > 2.4f ? "run" : "walk"), 0.15f);
    if (e->token && absf(dx) < reach + 0.15f && absf(dz) < 0.45f && e->cd == 0) {
      if (e->agent && crowd.id) cw_crowd_clear_target(crowd, AG(e->agent));
      e->cd = 0;
      enemy_pick_attack(e);
    } else if (e->token && e->def == D_RUNNER && absf(dz) < 0.35f && absf(dx) > 3.0f && absf(dx) < 5.5f) {
      enemy_pick_attack(e); /* the runner charges from a distance */
    } else if (e->token && e->t++ > 600) { /* a token held too long is returned */
      e->token = 0; W.tokens_out--; e->cd = 60; e->t = 0;
    }
    /* boss phases: a second wind below half health */
    if (boss && e->phase == 0 && e->hp < e->maxhp * 0.5f) {
      e->phase = 1;
      e->st = S_TAUNT; e->t = 0; char_restart(e, "taunt", 0.05f);
      if (e->def == D_RINGMASTER || e->def == D_FOREMAN)
        for (int k = 0; k < 2; k++) ent_spawn_enemy(D_THUG, W.cam_x + (k ? 9.0f : -9.0f), rf(-2, 2));
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
    for (int p = 0; p < MAXP; p++) {
      ent *e = &W.p[p];
      if (!e->used || e->st == S_DEAD || e->st == S_FALL || e->st == S_DOWN) continue;
      float dx = e->x - it->x, dz = e->z - it->z;
      if (dx * dx + dz * dz > 0.55f) continue;
      if (it->kind == 0) { e->hp = clampf(e->hp + e->maxhp * 0.3f, 0, e->maxhp); sfx(SFX_HEAL); fx_flash(it->x, 1, it->z, 0.3f, 1, 0.4f, 6); }
      else if (it->kind == 4) { e->score += 50; W.total_score += 50; W.coins++; sfx(SFX_PICK); }
      else if (!e->weapon) { e->weapon = it->kind; e->wdur = it->kind == 3 ? 6 : 9; weapon_show(e); sfx(SFX_PICK); }
      else continue;
      it->used = false;
      break;
    }
    if (it->used && it->kind != 4 && it->t > 60 * 40) it->used = false;
  }
}

/* ── the match ──────────────────────────────────────────────────────── */
static void free_all(void) {
  for (int i = 0; i < MAXP; i++) if (W.p[i].used) { char_free(&W.p[i]); W.p[i].used = false; }
  for (int i = 0; i < MAXE; i++) if (W.e[i].used) { char_free(&W.e[i]); W.e[i].used = false; }
}

void sim_reset(int players, int stage) {
  bool keep = stage > 0 && players == W.players;
  ent old[MAXP];
  memcpy(old, W.p, sizeof old);
  int total = W.total_score;
  if (crowd.id) { cw_crowd_free(crowd); crowd = (cw_crowd){ 0 }; }
  free_all();
  if (!keep) { memset(&W, 0, sizeof W); cw_rng_seed(&W.rng, 20260101); }
  else { memset(W.e, 0, sizeof W.e); memset(W.item, 0, sizeof W.item); memset(W.prop, 0, sizeof W.prop); W.wave = 0; W.kills = 0; W.boss_active = false; W.combo_n = 0; W.tokens_out = 0; }
  W.players = players, W.stage = stage;
  W.total_score = keep ? total : 0;
  W.phase = PH_INTRO, W.phase_t = 0;
  W.cam_x = 0, W.slow = 0;
  level_build(stage);
  crowd_setup();
  static const int PD[MAXP] = { D_ROOK, D_MIKA, D_JET, D_ROOK };
  for (int i = 0; i < players; i++) {
    int def = PD[i];
    ent_init(&W.p[i], def, true, -3.0f + (float)i * 0.9f, -1.2f + (float)i * 0.8f);
    W.p[i].lives = keep ? old[i].lives : 3;
    W.p[i].score = keep ? old[i].score : 0;
    if (keep) { W.p[i].hp = W.p[i].maxhp * 0.5f + old[i].hp * 0.5f; if (W.p[i].hp > W.p[i].maxhp) W.p[i].hp = W.p[i].maxhp; if (old[i].hp <= 0) W.p[i].hp = W.p[i].maxhp * 0.6f; }
    W.p[i].idx = i;
    W.p[i].face = 1;
  }
  stage_wave_load(stage, 0);
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
  items_update();

  /* waves */
  if (play) {
    bool queue_left = false;
    for (int i = 0; i < W.spawn_q_n; i++) if (W.spawn_q[i].def >= 0) queue_left = true;
    if (!queue_left && enemies_alive() == 0 && W.wave_spawned) {
      int waves = stage_wave_count(W.stage);
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
    if (!anyone) W.phase = PH_OVER, W.phase_t = 0;
  }
  level_camera(1.0f / TPS);
  level_update();
  fx_tick();

  /* the hash the gates compare */
  uint32_t h = W.hash;
  for (int i = 0; i < MAXP; i++) if (W.p[i].used) { int32_t q[4] = { (int32_t)(W.p[i].x * 1000), (int32_t)(W.p[i].z * 1000), (int32_t)W.p[i].hp, W.p[i].st }; const uint8_t *b = (const uint8_t *)q; for (size_t j = 0; j < sizeof q; j++) h = (h ^ b[j]) * 16777619u; }
  for (int i = 0; i < MAXE; i++) if (W.e[i].used) { int32_t q[4] = { (int32_t)(W.e[i].x * 1000), (int32_t)(W.e[i].z * 1000), (int32_t)W.e[i].hp, W.e[i].st }; const uint8_t *b = (const uint8_t *)q; for (size_t j = 0; j < sizeof q; j++) h = (h ^ b[j]) * 16777619u; }
  W.hash = h;
}

/* ── node sync (render side) ────────────────────────────────────────── */
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
  float yaw = (float)e->face * 1.5708f;
  /* lean a little toward the lane when walking */
  cw_node3d_set_rotation(e->root, (t3_vec3){ 0, yaw, 0 });
  if (e->weapon && e->wnode.id) {
    t3_vec3 h = cw_skeleton_bone_world(e->model, "HandR");
    (void)HAND_OFF;
    cw_node3d_set_position(e->wnode, (t3_vec3){ h.x, h.y + 0.1f, h.z });
  }
  (void)ent_index;
}
